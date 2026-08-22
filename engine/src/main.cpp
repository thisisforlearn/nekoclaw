#include "nekoclaw/uci.h"
#include "nekoclaw/bench.h"
#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv){
  std::string arg;
  for(int i=1;i<argc;++i) arg+=std::string(argv[i])+" ";
  if(arg.find("--console")!=std::string::npos || arg.find("--play")!=std::string::npos){
    nekoclaw::console_loop();
    return 0;
  }
  if(arg.find("bench")!=std::string::npos){
    int d=14;
    // parse bench depth
    size_t p=arg.find("bench");
    if(p!=std::string::npos){
      std::string after=arg.substr(p+5);
      d=std::atoi(after.c_str()); if(d<=0) d=14;
    }
    nekoclaw::bench(d);
    return 0;
  }
  // default: UCI loop
  nekoclaw::uci_loop();
  return 0;
}
