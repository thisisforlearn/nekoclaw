#include "nekoclaw/nnue/simd.h"
#include "nekoclaw/nnue/arch.h"
#include <immintrin.h>
#include <cstdint>
#include <algorithm>

namespace nekoclaw::nnue {

// Fallbacks
void scalar_add(int16_t* acc, const int16_t* w){
  for(int i=0;i<kFTSize;++i) acc[i]+=w[i];
}
void scalar_sub(int16_t* acc, const int16_t* w){
  for(int i=0;i<kFTSize;++i) acc[i]-=w[i];
}

#ifdef HAS_AVX2
// AVX2 256-bit => 16 x int16 per iteration (1024 = 64 iterations)
void avx2_add(int16_t* acc, const int16_t* w){
  for(int i=0;i<kFTSize;i+=16){
    __m256i a=_mm256_load_si256((__m256i*)(acc+i));
    __m256i b=_mm256_load_si256((__m256i*)(w+i));
    a=_mm256_add_epi16(a,b);
    _mm256_store_si256((__m256i*)(acc+i),a);
  }
}
void avx2_sub(int16_t* acc, const int16_t* w){
  for(int i=0;i<kFTSize;i+=16){
    __m256i a=_mm256_load_si256((__m256i*)(acc+i));
    __m256i b=_mm256_load_si256((__m256i*)(w+i));
    a=_mm256_sub_epi16(a,b);
    _mm256_store_si256((__m256i*)(acc+i),a);
  }
}
void avx2_add_add_sub(int16_t* acc, const int16_t* wA1, const int16_t* wA2, const int16_t* wS){
  for(int i=0;i<kFTSize;i+=16){
    __m256i a=_mm256_load_si256((__m256i*)(acc+i));
    __m256i b1=_mm256_load_si256((__m256i*)(wA1+i));
    __m256i b2=_mm256_load_si256((__m256i*)(wA2+i));
    __m256i s=_mm256_load_si256((__m256i*)(wS+i));
    a=_mm256_add_epi16(a,b1);
    a=_mm256_add_epi16(a,b2);
    a=_mm256_sub_epi16(a,s);
    _mm256_store_si256((__m256i*)(acc+i),a);
  }
}
void avx2_add_sub(int16_t* acc, const int16_t* wAdd, const int16_t* wSub){
  for(int i=0;i<kFTSize;i+=16){
    __m256i a=_mm256_load_si256((__m256i*)(acc+i));
    __m256i ad=_mm256_load_si256((__m256i*)(wAdd+i));
    __m256i su=_mm256_load_si256((__m256i*)(wSub+i));
    a=_mm256_add_epi16(a,ad);
    a=_mm256_sub_epi16(a,su);
    _mm256_store_si256((__m256i*)(acc+i),a);
  }
}

void avx2_screlu_concatenate(const int16_t* accW, const int16_t* accB, int8_t* out){
  // SCReLU: clamp 0..QA, square, scale to QB (64)
  // out_i = min(127, (clamp*clamp*QB)/(QA*QA) ) for positive, 0 else
  // We use 32-bit intermediate
  for(int i=0;i<kFTSize;++i){
    int vW = accW[i];
    if(vW<0) vW=0;
    else if(vW>kFTQS) vW=kFTQS;
    int sqW = (vW * vW * kHiddenQS) / (kFTQS*kFTQS); // 0..64
    if(sqW>127) sqW=127;
    out[i]=int8_t(sqW);
  }
  for(int i=0;i<kFTSize;++i){
    int vB = accB[i];
    if(vB<0) vB=0;
    else if(vB>kFTQS) vB=kFTQS;
    int sqB = (vB * vB * kHiddenQS) / (kFTQS*kFTQS);
    if(sqB>127) sqB=127;
    out[kFTSize + i]=int8_t(sqB);
  }
  // TODO: AVX2 vectorized version using _mm256_clip? For now scalar is fine for 2048 elems (~2us)
}

void avx2_screlu_concatenate_int16(const int16_t* accW, const int16_t* accB, int16_t* out){
  for(int i=0;i<kFTSize;++i){
    int v=accW[i]; if(v<0) v=0; else if(v>kFTQS) v=kFTQS;
    out[i]=int16_t((v*v)/kFTQS);
  }
  for(int i=0;i<kFTSize;++i){
    int v=accB[i]; if(v<0) v=0; else if(v>kFTQS) v=kFTQS;
    out[kFTSize+i]=int16_t((v*v)/kFTQS);
  }
}

void avx2_affine_2048x16(const int8_t* in, const int8_t* weights, const int32_t* bias, int32_t* out){
  for(int o=0;o<16;++o){
    int32_t sum=bias[o];
    const int8_t* w = weights + o*kL1In;
    // manual unroll 8
    for(int i=0;i<kL1In;++i) sum += int(in[i]) * int(w[i]);
#ifdef HAS_AVXVNNI
    // VNNI would use _mm256_dpbusd_epi32 for 4x throughput — placeholder
#endif
    if(sum<0) sum=0;
    out[o]=sum;
  }
}
void avx2_affine_16x32(const int8_t* in, const int8_t* w, const int32_t* bias, int32_t* out){
  for(int o=0;o<32;++o){
    int32_t sum=bias[o];
    const int8_t* wo=w+o*16;
    for(int i=0;i<16;++i) sum+= int(in[i])*int(wo[i]);
    if(sum<0) sum=0;
    out[o]=sum;
  }
}
void avx2_affine_32x32(const int8_t* in, const int8_t* w, const int32_t* bias, int32_t* out){
  for(int o=0;o<32;++o){
    int32_t sum=bias[o];
    const int8_t* wo=w+o*32;
    for(int i=0;i<32;++i) sum+= int(in[i])*int(wo[i]);
    if(sum<0) sum=0;
    out[o]=sum;
  }
}
void avx2_affine_32x1(const int8_t* in, const int8_t* w, int32_t bias, int32_t* out){
  int32_t sum=bias;
  for(int i=0;i<32;++i) sum+= int(in[i])*int(w[i]);
  *out=sum;
}

#else
// no AVX2 fallback uses scalar
void avx2_add(int16_t* acc, const int16_t* w){ scalar_add(acc,w); }
void avx2_sub(int16_t* acc, const int16_t* w){ scalar_sub(acc,w); }
void avx2_add_add_sub(int16_t* acc, const int16_t* a1,const int16_t* a2,const int16_t* s){ scalar_add(acc,a1); scalar_add(acc,a2); scalar_sub(acc,s); }
void avx2_add_sub(int16_t* acc,const int16_t* a,const int16_t* s){ scalar_add(acc,a); scalar_sub(acc,s); }
void avx2_screlu_concatenate(const int16_t* w,const int16_t* b,int8_t* o){ for(int i=0;i<kFTSize;++i){int v=w[i]; if(v<0)v=0;else if(v>kFTQS)v=kFTQS; int sq=(v*v*kHiddenQS)/(kFTQS*kFTQS); if(sq>127)sq=127; o[i]=int8_t(sq);} for(int i=0;i<kFTSize;++i){int v=b[i]; if(v<0)v=0;else if(v>kFTQS)v=kFTQS; int sq=(v*v*kHiddenQS)/(kFTQS*kFTQS); if(sq>127)sq=127; o[kFTSize+i]=int8_t(sq);} }
void avx2_screlu_concatenate_int16(const int16_t* w,const int16_t* b,int16_t* o){ for(int i=0;i<kFTSize;++i){int v=w[i]; if(v<0)v=0;else if(v>kFTQS)v=kFTQS; o[i]=int16_t((v*v)/kFTQS);} for(int i=0;i<kFTSize;++i){int v=b[i]; if(v<0)v=0;else if(v>kFTQS)v=kFTQS; o[kFTSize+i]=int16_t((v*v)/kFTQS);} }
void avx2_affine_2048x16(const int8_t* in,const int8_t* w,const int32_t* b,int32_t* o){ for(int k=0;k<16;++k){int32_t s=b[k]; const int8_t* wo=w+k*kL1In; for(int i=0;i<kL1In;++i) s+=int(in[i])*int(wo[i]); if(s<0)s=0; o[k]=s; } }
void avx2_affine_16x32(const int8_t* in,const int8_t* w,const int32_t* b,int32_t* o){ for(int k=0;k<32;++k){int32_t s=b[k]; const int8_t* wo=w+k*16; for(int i=0;i<16;++i) s+=int(in[i])*int(wo[i]); if(s<0)s=0; o[k]=s; } }
void avx2_affine_32x32(const int8_t* in,const int8_t* w,const int32_t* b,int32_t* o){ for(int k=0;k<32;++k){int32_t s=b[k]; const int8_t* wo=w+k*32; for(int i=0;i<32;++i) s+=int(in[i])*int(wo[i]); if(s<0)s=0; o[k]=s; } }
void avx2_affine_32x1(const int8_t* in,const int8_t* w,int32_t b,int32_t* o){ int32_t s=b; for(int i=0;i<32;++i) s+=int(in[i])*int(w[i]); *o=s; }
#endif

} // namespace
