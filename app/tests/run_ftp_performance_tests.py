from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include "nexus/network_socket_config.hpp"
#include "nexus/notification_throttle.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
struct Config{int tcp_tx_buf_size=32768,tcp_tx_buf_max_size=262144,tcp_rx_buf_size=65536,tcp_rx_buf_max_size=262144,udp_tx_buf_size=9216,udp_rx_buf_size=42240,sb_efficiency=4,bsd_service_type=3,num_bsd_sessions=3;};
int main(){
 Config original;const auto ftp=nexus::network::TransferSocketConfig(original);
 assert(ftp.tcp_rx_buf_size==524288&&ftp.tcp_rx_buf_max_size==1048576&&ftp.tcp_tx_buf_size==524288&&ftp.tcp_tx_buf_max_size==1048576);
 assert(ftp.udp_tx_buf_size==original.udp_tx_buf_size&&ftp.udp_rx_buf_size==original.udp_rx_buf_size&&ftp.sb_efficiency==original.sb_efficiency&&ftp.num_bsd_sessions==original.num_bsd_sessions&&ftp.bsd_service_type==original.bsd_service_type);
 assert(nexus::network::SocketTransferMemorySize(ftp)<9*1024*1024);assert(original.tcp_rx_buf_size==65536);
 nexus::NotificationThrottle throttle;unsigned calls=0;
 for(std::uint64_t ms=0;ms<1000;++ms)for(int chunk=0;chunk<20;++chunk)calls+=throttle.Ready(ms);
 assert(calls==4);assert(throttle.Ready(1000));assert(!throttle.Ready(1001));assert(throttle.Ready(0));
 std::cout<<"FTP performance controls: bounded bidirectional buffers, preserved SDK settings, 20,000 progress events reduced to four LED notifications per second passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='nexus-ftp-performance-') as tmp:
 p=Path(tmp);(p/'test.cpp').write_text(code)
 for opt in ('-O0','-O2'):
  subprocess.run(['g++','-std=c++20',opt,'-UNDEBUG','-Wall','-Wextra','-Werror','-fsanitize=undefined','-I',str(root/'sphaira/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
