#include <helpers/utils.hpp>
#include <iostream>
#include <state/data-structures.hpp>
#include <string>

namespace state {

/*int https_port = 47984;
int http_port = 47989;
int control_port = 47999;    // udp
int video_ping_port = 48100; // udp
int audio_ping_port = 48200; // udp
int rtsp_setup_port = 48010; // tcp

void init_ports() {
  auto base_port_str = std::string(utils::get_env("WOLF_BASE_PORT", "47984"));
  if (base_port_str.empty()) {
    return;
  }
  try {
    int base_https_port = std::stoi(base_port_str);
    https_port = base_https_port;
    http_port = base_https_port + 5;
    control_port = base_https_port + 15;
    video_ping_port = base_https_port + 116;
    audio_ping_port = base_https_port + 216;
    rtsp_setup_port = base_https_port + 26;
  } catch (const std::exception &e) {
    std::cout << e.what() << std::endl;
    std::exit(1);
  }
  return;
}

int HTTPS_PORT() {
  return https_port;
}

int HTTP_PORT() {
  return http_port;
}

int CONTROL_PORT() {
  return control_port;
}

int VIDEO_PING_PORT() {
  return video_ping_port;
}
int AUDIO_PING_PORT() {
  return audio_ping_port;
}

int RTSP_SETUP_PORT() {
  return rtsp_setup_port;
}*/

} // namespace state
