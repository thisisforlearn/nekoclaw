#include "nekoclaw/nnue/weights.h"
#include <fstream>
#include <cstring>

namespace nekoclaw::nnue {

NetworkWeights g_weights;
bool g_weights_loaded=false;

bool NetworkWeights::is_zero() const {
  for(size_t i=0;i<kFTWeights;++i) if(ft_weights[i]!=0) return false;
  return true;
}

bool load_weights(const std::string& path, std::string& err){
  std::ifstream f(path, std::ios::binary);
  if(!f){ err="cannot open "+path; g_weights_loaded=false; return false; }
  uint32_t magic, version, arch;
  f.read((char*)&magic,4); f.read((char*)&version,4); f.read((char*)&arch,4);
  if(magic!=kNnueVersion){ err="bad magic"; return false; }
  if(arch!=kNnueArchHash){ err="arch mismatch: expected 1024x8 SCReLU"; return false; }
  // FT
  f.read((char*)g_weights.ft_weights, sizeof(g_weights.ft_weights));
  f.read((char*)g_weights.ft_bias, sizeof(g_weights.ft_bias));
  for(int b=0;b<kBucketCount;++b){
    auto &bb=g_weights.buckets[b];
    f.read((char*)bb.l1_weights, sizeof(bb.l1_weights));
    f.read((char*)bb.l1_bias, sizeof(bb.l1_bias));
    f.read((char*)bb.l2_weights, sizeof(bb.l2_weights));
    f.read((char*)bb.l2_bias, sizeof(bb.l2_bias));
    f.read((char*)bb.l3_weights, sizeof(bb.l3_weights));
    f.read((char*)bb.l3_bias, sizeof(bb.l3_bias));
    f.read((char*)bb.out_weights, sizeof(bb.out_weights));
    f.read((char*)&bb.out_bias, sizeof(bb.out_bias));
  }
  if(!f){ err="truncated file"; return false; }
  g_weights_loaded=true;
  return true;
}

bool save_weights(const std::string& path, std::string& err){
  std::ofstream f(path, std::ios::binary);
  if(!f){ err="cannot write "+path; return false; }
  uint32_t magic=kNnueVersion, version=1, arch=kNnueArchHash;
  f.write((char*)&magic,4); f.write((char*)&version,4); f.write((char*)&arch,4);
  f.write((char*)g_weights.ft_weights, sizeof(g_weights.ft_weights));
  f.write((char*)g_weights.ft_bias, sizeof(g_weights.ft_bias));
  for(int b=0;b<kBucketCount;++b){
    auto &bb=g_weights.buckets[b];
    f.write((char*)bb.l1_weights, sizeof(bb.l1_weights));
    f.write((char*)bb.l1_bias, sizeof(bb.l1_bias));
    f.write((char*)bb.l2_weights, sizeof(bb.l2_weights));
    f.write((char*)bb.l2_bias, sizeof(bb.l2_bias));
    f.write((char*)bb.l3_weights, sizeof(bb.l3_weights));
    f.write((char*)bb.l3_bias, sizeof(bb.l3_bias));
    f.write((char*)bb.out_weights, sizeof(bb.out_weights));
    f.write((char*)&bb.out_bias, sizeof(bb.out_bias));
  }
  return true;
}

} // namespace
