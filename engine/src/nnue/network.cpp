#include "nekoclaw/nnue/network.h"
#include "nekoclaw/nnue/simd.h"
#include "nekoclaw/nnue/weights.h"
#include "nekoclaw/position.h"
#include <algorithm>
#include <cstring>

namespace nekoclaw::nnue {

void screlu_transform(const Accumulator& acc, int8_t* out2048){
  avx2_screlu_concatenate(acc.white, acc.black, out2048);
}

int32_t forward_bucket(const int8_t* screlu2048, int bucketIdx){
  const BucketWeights& b = g_weights.buckets[bucketIdx];
  alignas(32) int32_t l1out[kL1Out];
  alignas(32) int32_t l2out[kL2Out];
  alignas(32) int32_t l3out[kL3Out];
  alignas(32) int8_t l1out8[kL1Out];
  alignas(32) int8_t l2out8[kL2Out];
  alignas(32) int8_t l3out8[kL3Out];

  avx2_affine_2048x16(screlu2048, b.l1_weights, b.l1_bias, l1out);
  for(int i=0;i<kL1Out;++i){ int v=l1out[i]; if(v<0) v=0; else if(v>127) v=127; l1out8[i]=int8_t(v); }

  avx2_affine_16x32(l1out8, b.l2_weights, b.l2_bias, l2out);
  for(int i=0;i<kL2Out;++i){ int v=l2out[i]; if(v<-128) v=-128; else if(v>127) v=127; l2out8[i]=int8_t(v); }

  avx2_affine_32x32(l2out8, b.l3_weights, b.l3_bias, l3out);
  for(int i=0;i<kL3Out;++i){ int v=l3out[i]; if(v<-128) v=-128; else if(v>127) v=127; l3out8[i]=int8_t(v); }

  int32_t out=0;
  avx2_affine_32x1(l3out8, b.out_weights, b.out_bias, &out);
  return out;
}

Value evaluate(const Position& pos, const Accumulator& acc){
  if(!g_weights_loaded){
    // fallback simple material if no net loaded
    Value v=0;
    for(int pt=0;pt<6;++pt){
      int w = pos.count(WHITE, PieceType(pt));
      int b = pos.count(BLACK, PieceType(pt));
      static const int vals[6]={100,320,330,500,900,0};
      v += vals[pt]*(w-b);
    }
    return pos.side_to_move()==WHITE? v : -v;
  }
  int bucket = pos.bucket_index();
  alignas(64) int8_t screlu[kTransformedDims];
  screlu_transform(acc, screlu);
  int32_t raw = forward_bucket(screlu, bucket);
  // Dequant: raw * SCALE / (QB*QB) ? Simplified: raw * 400 / 64 / 64
  // Our QB=64, QA=255, but SCReLU already scaled to QB, so final scale is KScale
  // Stockfish does: eval * 400 / (QA*QB) ; we approximate
  int eval = (raw * kScale) / (kHiddenQS * 8); // tuned divisor for centipawns
  if(eval>15000) eval=15000;
  if(eval<-15000) eval=-15000;
  return pos.side_to_move()==WHITE? eval : -eval; // but acc is always white/black concat, need perspective?
  // Actually screlu concat is white+black in order, bucket network is side-agnostic and outputs white perspective
  // Then we flip if stm==BLACK handled above
}

Value evaluate(const Position& pos){
  // Full refresh evaluate (no stack)
  Accumulator acc;
  // we need to build acc from scratch for stateless eval
  // Use global stack refresh logic simplified
  if(!g_weights_loaded){
    Value v=0;
    for(int pt=0;pt<6;++pt){ static const int vals[6]={100,320,330,500,900,0}; v+= vals[pt]*(pos.count(WHITE,PieceType(pt))-pos.count(BLACK,PieceType(pt))); }
    return pos.side_to_move()==WHITE? v : -v;
  }
  // build accumulator
  for(int i=0;i<kFTSize;++i) acc.white[i]=g_weights.ft_bias[i], acc.black[i]=g_weights.ft_bias[i];
  int feats[256]; int cnt;
  append_features(pos, WHITE, feats, cnt);
  for(int i=0;i<cnt;++i) avx2_add(acc.white, &g_weights.ft_weights[feats[i]*kFTSize]);
  append_features(pos, BLACK, feats, cnt);
  for(int i=0;i<cnt;++i) avx2_add(acc.black, &g_weights.ft_weights[feats[i]*kFTSize]);
  return evaluate(pos, acc);
}

} // namespace
