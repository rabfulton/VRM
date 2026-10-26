#include "viseme.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static VisemeAnalyzer analyzer;
int main(void) {
    int16_t pcm[2400];
    for(int i=0;i<2400;i++)pcm[i]=(int16_t)(10000*sinf(6.2831853f*500*i/24000));
    viseme_init(&analyzer);
    /* PCM ingest must produce mouth shapes with no enable/configuration call. */
    viseme_analyze(&analyzer,pcm,2400,0);
    VisemeFrame frame;
    assert(viseme_sample(&analyzer,1200,&frame));
    float sum=0;for(int i=0;i<VISEME_COUNT;i++)sum+=frame.weights[i];
    assert(sum>0.1f);
    viseme_reset(&analyzer);
    memset(pcm,0,sizeof(pcm));
    viseme_analyze(&analyzer,pcm,2400,0);
    assert(viseme_sample(&analyzer,1200,&frame));
    for(int i=0;i<VISEME_COUNT;i++)assert(frame.weights[i]==0);
    viseme_free(&analyzer);
    puts("Lip-sync default test passed: voice drives visemes without enabling, silence/reset clears them");
}
