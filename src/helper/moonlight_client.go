package main

import (
	"bytes"
	"crypto/aes"
	"crypto/rand"
	"crypto/rsa"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"encoding/xml"
	"fmt"
	"io"
	"log"
	"math/big"
	"net/http"
	"net/url"
	"time"
)

// PairResponse XML 响应结构体
type PairResponse struct {
	XMLName           xml.Name `xml:"root"`
	StatusCode        string   `xml:"status_code,attr"`
	Paired            string   `xml:"paired"`
	PlainCert         string   `xml:"plaincert,omitempty"`
	ChallengeResponse string   `xml:"challengeresponse,omitempty"`
	PairingSecret     string   `xml:"pairingsecret,omitempty"`
	Error             string   `xml:"error,omitempty"`
}

// ServerInfo 服务器信息结构体
type ServerInfo struct {
	XMLName                xml.Name `xml:"root"`
	StatusCode             string   `xml:"status_code,attr"`
	Hostname               string   `xml:"hostname"`
	AppVersion             string   `xml:"appversion"`
	GfeVersion             string   `xml:"GfeVersion"`
	UniqueID               string   `xml:"uniqueid"`
	MaxLumaPixelsHEVC      string   `xml:"MaxLumaPixelsHEVC"`
	ServerCodecModeSupport string   `xml:"ServerCodecModeSupport"`
	HttpsPort              string   `xml:"HttpsPort"`
	ExternalPort           string   `xml:"ExternalPort"`
	Mac                    string   `xml:"mac"`
	LocalIP                string   `xml:"LocalIP"`
	SupportedDisplayMode   struct {
		DisplayMode []struct {
			Width       string `xml:"Width"`
			Height      string `xml:"Height"`
			RefreshRate string `xml:"RefreshRate"`
		} `xml:"DisplayMode"`
	} `xml:"SupportedDisplayMode"`
	PairStatus  string `xml:"PairStatus"`
	CurrentGame string `xml:"currentgame"`
	State       string `xml:"state"`
}

// App 应用程序结构体
type App struct {
	IsHdrSupported string `xml:"IsHdrSupported"`
	AppTitle       string `xml:"AppTitle"`
	ID             string `xml:"ID"`
}

// AppList 应用程序列表结构体
type AppList struct {
	XMLName    xml.Name `xml:"root"`
	StatusCode string   `xml:"status_code,attr"`
	Apps       []App    `xml:"App"`
}

// LaunchResponse 启动应用程序响应结构体
type LaunchResponse struct {
	XMLName     xml.Name `xml:"root"`
	StatusCode  string   `xml:"status_code,attr"`
	SessionURL0 string   `xml:"sessionUrl0,omitempty"`
	GameSession string   `xml:"gamesession,omitempty"`
	Error       string   `xml:"error,omitempty"`
}

// MoonlightPairingClient Moonlight 配对客户端
type MoonlightPairingClient struct {
	hostIP      string
	hostPort    int
	httpsPort   int
	clientIP    string
	clientID    string
	httpClient  *http.Client
	httpsClient *http.Client

	// 配对过程中的状态
	aesKey          []byte
	serverChallenge string
	clientChallenge string
	clientSecret    string
	clientHash      string
	privateKey      *rsa.PrivateKey
	clientCert      string
}

// NewMoonlightPairingClient 创建新的配对客户端
func NewMoonlightPairingClient(hostIP string, httpPort int, clientIP, clientID string) *MoonlightPairingClient {
	// 创建 HTTP 客户端
	httpClient := &http.Client{
		Timeout: 30 * time.Second,
	}

	// 创建 HTTPS 客户端（忽略证书验证）
	httpsClient := &http.Client{
		Timeout: 30 * time.Second,
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{
				InsecureSkipVerify: true,
			},
		},
	}
	httpsPort := httpPort - 5

	return &MoonlightPairingClient{
		hostIP:      hostIP,
		hostPort:    httpPort,
		httpsPort:   httpsPort,
		clientIP:    clientIP,
		clientID:    clientID,
		httpClient:  httpClient,
		httpsClient: httpsClient,
	}
}

// generateRandomBytes 生成随机字节
func (c *MoonlightPairingClient) generateRandomBytes(length int) ([]byte, error) {
	bytes := make([]byte, length)
	_, err := rand.Read(bytes)
	return bytes, err
}

// generateAESKey 根据 salt 和 PIN 生成 AES 密钥
func (c *MoonlightPairingClient) generateAESKey(salt, pin string) []byte {
	// 根据服务端逻辑：SHA256(SALT + PIN)[0:16]
	data := salt + pin
	hash := sha256.Sum256([]byte(data))
	return hash[:16]
}

// generateClientCertificate 生成客户端证书（简化版本）
func (c *MoonlightPairingClient) generateClientCertificate() (string, *rsa.PrivateKey, error) {
	// 生成 RSA 密钥对
	privateKey, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		return "", nil, err
	}

	// 创建证书模板
	template := x509.Certificate{
		SerialNumber: big.NewInt(1),
		Subject: pkix.Name{
			CommonName: c.clientID,
		},
		NotBefore:   time.Now(),
		NotAfter:    time.Now().Add(365 * 24 * time.Hour),
		KeyUsage:    x509.KeyUsageKeyEncipherment | x509.KeyUsageDigitalSignature,
		ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth, x509.ExtKeyUsageClientAuth},
	}

	// 创建证书
	derBytes, err := x509.CreateCertificate(rand.Reader, &template, &template, &privateKey.PublicKey, privateKey)
	if err != nil {
		return "", nil, err
	}

	// 编码为 PEM 格式
	certPEM := pem.EncodeToMemory(&pem.Block{
		Type:  "CERTIFICATE",
		Bytes: derBytes,
	})

	return string(certPEM), privateKey, nil
}

// getClientCertSignature 获取客户端证书签名
func (c *MoonlightPairingClient) getClientCertSignature(clientCert string) (string, error) {
	// 解析证书
	block, _ := pem.Decode([]byte(clientCert))
	if block == nil {
		return "", fmt.Errorf("failed to decode certificate")
	}

	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return "", err
	}

	// 计算证书的 SHA256 哈希
	hash := sha256.Sum256(cert.Raw)
	return hex.EncodeToString(hash[:]), nil
}

// encryptAES 使用 AES 加密数据
func (c *MoonlightPairingClient) encryptAES(data []byte, key []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	// 使用 ECB 模式（与服务端保持一致）
	paddedData := c.pkcs7Pad(data, aes.BlockSize)
	encrypted := make([]byte, len(paddedData))

	for i := 0; i < len(paddedData); i += aes.BlockSize {
		block.Encrypt(encrypted[i:i+aes.BlockSize], paddedData[i:i+aes.BlockSize])
	}

	return encrypted, nil
}

// decryptAES 使用 AES 解密数据
func (c *MoonlightPairingClient) decryptAES(data []byte, key []byte) ([]byte, error) {
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}

	decrypted := make([]byte, len(data))
	for i := 0; i < len(data); i += aes.BlockSize {
		block.Decrypt(decrypted[i:i+aes.BlockSize], data[i:i+aes.BlockSize])
	}

	return c.pkcs7Unpad(decrypted), nil
}

// pkcs7Pad PKCS7 填充
func (c *MoonlightPairingClient) pkcs7Pad(data []byte, blockSize int) []byte {
	padding := blockSize - len(data)%blockSize
	padtext := bytes.Repeat([]byte{byte(padding)}, padding)
	return append(data, padtext...)
}

// pkcs7Unpad PKCS7 去除填充
func (c *MoonlightPairingClient) pkcs7Unpad(data []byte) []byte {
	length := len(data)
	if length == 0 {
		return data
	}
	padding := int(data[length-1])
	if padding > length {
		return data
	}
	return data[:length-padding]
}

// makePairRequest 发送配对请求
func (c *MoonlightPairingClient) makePairRequest(params url.Values) (*http.Response, error) {
	baseURL := fmt.Sprintf("http://%s:%d/pair", c.hostIP, c.hostPort)
	req, err := http.NewRequest("GET", baseURL+"?"+params.Encode(), nil)
	if err != nil {
		return nil, err
	}

	// 设置必要的头部
	req.Header.Set("User-Agent", "Moonlight-Go-Client/1.0")

	return c.httpClient.Do(req)
}

// parseXMLResponse 解析 XML 响应
func (c *MoonlightPairingClient) parseXMLResponse(resp *http.Response) (map[string]string, error) {
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("读取响应体失败: %v", err)
	}

	// 记录响应内容用于调试
	bodyStr := string(body)
	log.Printf("======= 响应内容: %s", bodyStr)

	// 检查响应状态码
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("服务器返回错误状态码: %d, 响应: %s", resp.StatusCode, bodyStr)
	}

	// 使用 XML 解析器解析响应
	var pairResp PairResponse
	if err := xml.Unmarshal(body, &pairResp); err != nil {
		return nil, fmt.Errorf("解析 XML 响应失败: %v", err)
	}

	// 构建结果映射
	result := make(map[string]string)

	// 设置配对状态
	if pairResp.Paired != "" {
		result["paired"] = pairResp.Paired
	}

	// 设置其他字段
	if pairResp.PlainCert != "" {
		result["plaincert"] = pairResp.PlainCert
	}

	if pairResp.ChallengeResponse != "" {
		result["challengeresponse"] = pairResp.ChallengeResponse
	}

	if pairResp.PairingSecret != "" {
		result["pairingsecret"] = pairResp.PairingSecret
	}

	// 检查是否有错误
	if pairResp.Error != "" {
		return nil, fmt.Errorf("服务器返回错误: %s", pairResp.Error)
	}

	log.Printf("解析结果: %+v", result)
	log.Printf("---pair resp: %+v", pairResp)
	return result, nil
}

// parseServerInfoResponse 解析服务器信息响应
func (c *MoonlightPairingClient) parseServerInfoResponse(resp *http.Response) (*ServerInfo, error) {
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("读取响应体失败: %v", err)
	}

	// 记录响应内容用于调试
	bodyStr := string(body)
	log.Printf("======= 服务器信息响应: %s", bodyStr)

	// 检查响应状态码
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("服务器返回错误状态码: %d, 响应: %s", resp.StatusCode, bodyStr)
	}

	// 使用 XML 解析器解析响应
	var serverInfo ServerInfo
	if err := xml.Unmarshal(body, &serverInfo); err != nil {
		return nil, fmt.Errorf("解析服务器信息 XML 失败: %v", err)
	}

	log.Printf("服务器信息解析结果: %+v", serverInfo)
	return &serverInfo, nil
}

// parseAppListResponse 解析应用程序列表响应
func (c *MoonlightPairingClient) parseAppListResponse(resp *http.Response) (*AppList, error) {
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("读取响应体失败: %v", err)
	}

	// 记录响应内容用于调试
	bodyStr := string(body)
	log.Printf("======= 应用列表响应: %s", bodyStr)

	// 检查响应状态码
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("服务器返回错误状态码: %d, 响应: %s", resp.StatusCode, bodyStr)
	}

	// 使用 XML 解析器解析响应
	var appList AppList
	if err := xml.Unmarshal(body, &appList); err != nil {
		return nil, fmt.Errorf("解析应用列表 XML 失败: %v", err)
	}

	log.Printf("应用列表解析结果: %+v", appList)
	return &appList, nil
}

// parseLaunchResponse 解析启动应用程序响应
func (c *MoonlightPairingClient) parseLaunchResponse(resp *http.Response) (*LaunchResponse, error) {
	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, fmt.Errorf("读取响应体失败: %v", err)
	}

	// 记录响应内容用于调试
	bodyStr := string(body)
	log.Printf("======= 启动应用响应: %s", bodyStr)

	// 检查响应状态码
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("服务器返回错误状态码: %d, 响应: %s", resp.StatusCode, bodyStr)
	}

	// 使用 XML 解析器解析响应
	var launchResp LaunchResponse
	if err := xml.Unmarshal(body, &launchResp); err != nil {
		return nil, fmt.Errorf("解析启动应用 XML 失败: %v", err)
	}

	// 检查是否有错误
	if launchResp.Error != "" {
		return nil, fmt.Errorf("启动应用失败: %s", launchResp.Error)
	}

	log.Printf("启动应用解析结果: %+v", launchResp)
	return &launchResp, nil
}

// PairPhase1 配对阶段 1：发送客户端证书和 salt
func (c *MoonlightPairingClient) PairPhase1() error {
	log.Println("开始配对阶段 1...")

	// 生成随机 salt
	salt, err := c.generateRandomBytes(16)
	if err != nil {
		return err
	}
	saltHex := hex.EncodeToString(salt)

	// 生成客户端证书
	clientCert, privateKey, err := c.generateClientCertificate()
	if err != nil {
		return fmt.Errorf("生成客户端证书失败: %v", err)
	}
	clientCertHex := hex.EncodeToString([]byte(clientCert))
	log.Printf("客户端证书长度: %d 字节", len(clientCert))

	// 保存私钥
	c.privateKey = privateKey

	// 保存证书
	c.clientCert = clientCert

	// 生成 AES 密钥
	c.aesKey = c.generateAESKey(saltHex, "6688") // 使用固定 PIN 码 6688

	// 构建请求参数
	params := url.Values{}
	params.Set("uniqueid", c.clientID)
	params.Set("salt", saltHex)
	params.Set("clientcert", clientCertHex)

	// 发送请求
	resp, err := c.makePairRequest(params)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	// 解析响应
	result, err := c.parseXMLResponse(resp)
	if err != nil {
		return err
	}

	if result["paired"] != "1" {
		return fmt.Errorf("配对阶段 1 失败")
	}

	log.Println("配对阶段 1 完成")
	return nil
}

// PairPhase2 配对阶段 2：发送客户端挑战
func (c *MoonlightPairingClient) PairPhase2() error {
	log.Println("开始配对阶段 2...")

	// 生成客户端挑战
	clientChallenge, err := c.generateRandomBytes(16)
	if err != nil {
		return err
	}
	c.clientChallenge = hex.EncodeToString(clientChallenge)

	// 构建请求参数
	params := url.Values{}
	params.Set("uniqueid", c.clientID)
	params.Set("clientchallenge", c.clientChallenge)

	// 发送请求
	resp, err := c.makePairRequest(params)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	// 解析响应
	result, err := c.parseXMLResponse(resp)
	if err != nil {
		return err
	}

	if result["paired"] != "1" {
		return fmt.Errorf("配对阶段 2 失败")
	}

	// 解密服务器挑战响应
	if challengeresponse, exists := result["challengeresponse"]; exists {
		encryptedData, err := hex.DecodeString(challengeresponse)
		if err != nil {
			return err
		}

		decryptedData, err := c.decryptAES(encryptedData, c.aesKey)
		if err != nil {
			return err
		}

		// 提取服务器挑战（前16字节）
		if len(decryptedData) >= 16 {
			c.serverChallenge = hex.EncodeToString(decryptedData[:16])
		}
	}

	log.Println("配对阶段 2 完成")
	return nil
}

// PairPhase3 配对阶段 3：发送服务器挑战响应
func (c *MoonlightPairingClient) PairPhase3() error {
	log.Println("开始配对阶段 3...")

	// 生成客户端密钥
	clientSecret, err := c.generateRandomBytes(16)
	if err != nil {
		return err
	}
	c.clientSecret = hex.EncodeToString(clientSecret)

	// 获取客户端证书签名
	clientCert, _, err := c.generateClientCertificate()
	if err != nil {
		return fmt.Errorf("生成客户端证书失败: %v", err)
	}
	clientCertSignature, err := c.getClientCertSignature(clientCert)
	if err != nil {
		return err
	}

	// 计算客户端哈希
	hashData := c.serverChallenge + clientCertSignature + c.clientSecret
	hash := sha256.Sum256([]byte(hashData))
	c.clientHash = hex.EncodeToString(hash[:])

	// 构建请求参数
	params := url.Values{}
	params.Set("uniqueid", c.clientID)
	params.Set("serverchallengeresp", c.clientHash)

	// 发送请求
	resp, err := c.makePairRequest(params)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	// 解析响应
	result, err := c.parseXMLResponse(resp)
	if err != nil {
		return err
	}

	if result["paired"] != "1" {
		return fmt.Errorf("配对阶段 3 失败")
	}

	log.Println("配对阶段 3 完成")
	return nil
}

// PairPhase4 配对阶段 4：发送客户端配对密钥
func (c *MoonlightPairingClient) PairPhase4() error {
	log.Println("开始配对阶段 4...")

	// 构建客户端配对密钥
	clientPairingSecret := c.clientSecret + c.clientHash

	// 构建请求参数
	params := url.Values{}
	params.Set("uniqueid", c.clientID)
	params.Set("clientpairingsecret", clientPairingSecret)

	// 发送请求
	resp, err := c.makePairRequest(params)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	// 解析响应
	result, err := c.parseXMLResponse(resp)
	if err != nil {
		return err
	}

	if result["paired"] != "1" {
		return fmt.Errorf("配对阶段 4 失败")
	}

	log.Println("配对阶段 4 完成")
	return nil
}

// PairPhase5 配对阶段 5：HTTPS 验证
func (c *MoonlightPairingClient) PairPhase5() error {
	log.Println("开始配对阶段 5 (HTTPS)...")

	// 检查私钥和证书是否已初始化
	if c.privateKey == nil || c.clientCert == "" {
		return fmt.Errorf("私钥或证书未初始化，请先完成阶段1")
	}

	// 使用保存的证书
	clientCert := c.clientCert

	// 解析证书
	block, _ := pem.Decode([]byte(clientCert))
	if block == nil {
		return fmt.Errorf("解析客户端证书失败")
	}

	cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return fmt.Errorf("解析证书失败: %v", err)
	}

	// 创建证书链
	certChain := []tls.Certificate{
		{
			Certificate: [][]byte{cert.Raw},
			PrivateKey:  c.privateKey,
		},
	}

	// 创建带客户端证书的HTTPS客户端
	httpsClientWithCert := &http.Client{
		Timeout: 30 * time.Second,
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{
				InsecureSkipVerify: true,
				Certificates:       certChain,
			},
		},
	}

	// 构建 HTTPS 请求
	httpsURL := fmt.Sprintf("https://%s:%d/pair", c.hostIP, c.httpsPort)
	params := url.Values{}
	params.Set("phrase", "pairchallenge")

	req, err := http.NewRequest("GET", httpsURL+"?"+params.Encode(), nil)
	if err != nil {
		return fmt.Errorf("创建HTTPS请求失败: %v", err)
	}

	// 设置必要的头部
	req.Header.Set("User-Agent", "Moonlight-Go-Client/1.0")

	log.Printf("发送HTTPS请求到: %s", httpsURL)

	// 发送请求
	resp, err := httpsClientWithCert.Do(req)
	if err != nil {
		return fmt.Errorf("HTTPS请求失败: %v", err)
	}
	defer resp.Body.Close()

	// 解析响应
	result, err := c.parseXMLResponse(resp)
	if err != nil {
		return fmt.Errorf("解析HTTPS响应失败: %v", err)
	}

	if result["paired"] != "1" {
		return fmt.Errorf("配对阶段 5 失败，响应: %+v", result)
	}

	log.Println("配对阶段 5 完成")

	// 配对成功后，设置带证书的 httpsClient
	block, _ = pem.Decode([]byte(c.clientCert))
	if block == nil {
		return fmt.Errorf("解析客户端证书失败")
	}
	x509Cert, err := x509.ParseCertificate(block.Bytes)
	if err != nil {
		return fmt.Errorf("解析证书失败: %v", err)
	}
	tlsCert := tls.Certificate{
		Certificate: [][]byte{x509Cert.Raw},
		PrivateKey:  c.privateKey,
	}
	c.httpsClient = &http.Client{
		Timeout: 30 * time.Second,
		Transport: &http.Transport{
			TLSClientConfig: &tls.Config{
				InsecureSkipVerify: true,
				Certificates:       []tls.Certificate{tlsCert},
			},
		},
	}

	return nil
}

// Pair 执行完整的配对流程
func (c *MoonlightPairingClient) Pair() error {
	log.Println("开始 Moonlight 配对流程...")

	// 阶段 1：发送客户端证书和 salt
	if err := c.PairPhase1(); err != nil {
		return fmt.Errorf("配对阶段 1 失败: %v", err)
	}

	// 阶段 2：发送客户端挑战
	if err := c.PairPhase2(); err != nil {
		return fmt.Errorf("配对阶段 2 失败: %v", err)
	}

	// 阶段 3：发送服务器挑战响应
	if err := c.PairPhase3(); err != nil {
		return fmt.Errorf("配对阶段 3 失败: %v", err)
	}

	// 阶段 4：发送客户端配对密钥
	if err := c.PairPhase4(); err != nil {
		return fmt.Errorf("配对阶段 4 失败: %v", err)
	}

	// 阶段 5：HTTPS 验证
	if err := c.PairPhase5(); err != nil {
		return fmt.Errorf("配对阶段 5 失败: %v", err)
	}

	log.Println("Moonlight 配对成功完成！")
	return nil
}

// 完整的客户端实现，包含配对后的功能
type MoonlightClient struct {
	*MoonlightPairingClient
	paired bool
}

// NewMoonlightClient 创建新的 Moonlight 客户端
func NewMoonlightClient(hostIP string, httpPort int, clientIP, clientID string) *MoonlightClient {
	return &MoonlightClient{
		MoonlightPairingClient: NewMoonlightPairingClient(hostIP, httpPort, clientIP, clientID),
		paired:                 false,
	}
}

// PairAndConnect 配对并连接
func (c *MoonlightClient) PairAndConnect() error {
	if err := c.Pair(); err != nil {
		return err
	}
	c.paired = true
	return nil
}

// GetServerInfo 获取服务器信息
func (c *MoonlightClient) GetServerInfo() (*ServerInfo, error) {
	serverURL := fmt.Sprintf("https://%s:%d/serverinfo", c.hostIP, c.httpsPort)

	resp, err := c.httpsClient.Get(serverURL)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return c.parseServerInfoResponse(resp)
}

// GetAppList 获取应用程序列表
func (c *MoonlightClient) GetAppList() (*AppList, error) {
	appURL := fmt.Sprintf("https://%s:%d/applist", c.hostIP, c.httpsPort)

	resp, err := c.httpsClient.Get(appURL)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	return c.parseAppListResponse(resp)
}

// LaunchApp 启动应用程序
func (c *MoonlightClient) LaunchApp(appID string, width, height, refreshRate int) (*LaunchResponse, error) {
	launchURL := fmt.Sprintf("https://%s:%d/launch", c.hostIP, c.httpsPort)

	params := url.Values{}
	params.Set("appid", appID)
	params.Set("mode", fmt.Sprintf("%dx%dx%d", width, height, refreshRate))
	params.Set("surroundAudioInfo", "196610") // 2 声道音频
	params.Set("rikey", "1234")               // 随机密钥
	params.Set("rikeyid", "5678")             // 随机密钥ID

	req, err := http.NewRequest("GET", launchURL+"?"+params.Encode(), nil)
	if err != nil {
		return nil, fmt.Errorf("创建启动请求失败: %v", err)
	}

	// 设置必要的头部
	req.Header.Set("User-Agent", "Moonlight-Go-Client/1.0")

	log.Printf("发送启动应用请求到: %s", launchURL+"?"+params.Encode())

	resp, err := c.httpsClient.Do(req)
	if err != nil {
		return nil, fmt.Errorf("启动应用请求失败: %v", err)
	}
	defer resp.Body.Close()

	return c.parseLaunchResponse(resp)
}

func main() {
	// 配置客户端
	client := NewMoonlightClient(
		"220.196.214.104", // 服务器 IP
		20008,             // HTTP 端口
		"192.168.1.100",   // 客户端 IP
		"go_client_001",   // 客户端 ID
	)

	// 执行配对
	if err := client.PairAndConnect(); err != nil {
		log.Fatalf("配对失败: %v", err)
	}

	log.Println("配对成功！")

	// 获取服务器信息
	serverInfo, err := client.GetServerInfo()
	if err != nil {
		log.Printf("获取服务器信息失败: %v", err)
	} else {
		log.Printf("服务器信息: 主机名=%s, 版本=%s, 状态=%s", serverInfo.Hostname, serverInfo.AppVersion, serverInfo.State)
		log.Printf("支持的显示模式数量: %d", len(serverInfo.SupportedDisplayMode.DisplayMode))
		for i, mode := range serverInfo.SupportedDisplayMode.DisplayMode {
			log.Printf("  模式%d: %sx%s@%sHz", i+1, mode.Width, mode.Height, mode.RefreshRate)
		}
	}

	// 获取应用程序列表
	appList, err := client.GetAppList()
	if err != nil {
		log.Printf("获取应用程序列表失败: %v", err)
	} else {
		log.Printf("应用程序列表: 共%d个应用", len(appList.Apps))
		for _, app := range appList.Apps {
			log.Printf("  应用: ID=%s, 标题=%s, HDR支持=%s", app.ID, app.AppTitle, app.IsHdrSupported)
		}
	}

	// 启动应用程序（示例）
	launchResult, err := client.LaunchApp("1", 1920, 1080, 60)
	if err != nil {
		log.Printf("启动应用程序失败: %v", err)
	} else {
		log.Printf("启动结果: 会话ID=%s", launchResult.SessionURL0)
	}
}
