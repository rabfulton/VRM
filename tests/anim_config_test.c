#define _POSIX_C_SOURCE 200809L
#include "anim_config.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char directory[] = "/tmp/vrm-anim-config-XXXXXX";
    assert(mkdtemp(directory));
    char animation[512], config[512];
    snprintf(animation, sizeof(animation), "%s/clip.vrma", directory);
    assert(anim_config_build_path(animation, config, sizeof(config)));
    FILE *file = fopen(config, "w"); assert(file);
    fprintf(file, "# Keep animation notes\nloopable=1\nloopable=0 # explicit one-shot\nstartup=1\nis_idle=0\nspring.drag=1.250\n");
    for(int i=0;i<180;i++) fprintf(file, "future.%d=value-%d\n", i,i);
    fprintf(file, "future.long=");
    for(int i=0;i<2500;i++) fputc('x',file);
    fprintf(file, "\nlast.setting=keep");
    fclose(file);
    AnimConfig cfg;
    assert(anim_config_load(animation, &cfg));
    assert(cfg.has_loopable && !cfg.loopable);
    assert(cfg.has_startup && cfg.startup && cfg.has_is_idle && !cfg.is_idle);
    assert(cfg.has_spring_drag && cfg.spring_drag == 1.25f);
    AnimConfig tuning = {.has_spring_stiffness=true,.spring_stiffness=1.5f};
    assert(anim_config_save(animation,&tuning));
    assert(anim_config_load(animation,&cfg));
    assert(cfg.has_loopable && !cfg.loopable && cfg.startup && !cfg.is_idle);
    assert(cfg.spring_stiffness == 1.5f && cfg.spring_drag == 1.25f);
    AnimConfig properties = {.has_loopable=true,.loopable=true,.has_startup=true,.startup=false,.has_is_idle=true,.is_idle=true,
                             .has_floor_contact=true,.floor_contact=false};
    assert(anim_config_save(animation,&properties));
    file=fopen(config,"r");assert(file);
    char *line=NULL;size_t cap=0;int loops=0,future=0;bool notes=false,last=false,long_value=false;
    while(getline(&line,&cap,file)>=0) {
        if(strncmp(line,"loopable=",9)==0){loops++;assert(strcmp(line,"loopable=1\n")==0);}
        if(strncmp(line,"future.",7)==0)future++;
        if(strstr(line,"Keep animation notes"))notes=true;
        if(strstr(line,"last.setting=keep"))last=true;
        if(strncmp(line,"future.long=",12)==0)long_value=strlen(line)>2500;
    }
    free(line);fclose(file);
    assert(loops==1 && future==181 && notes && last && long_value);
    assert(anim_config_load(animation,&cfg));
    assert(cfg.loopable && !cfg.startup && cfg.is_idle);
    assert(cfg.has_floor_contact && !cfg.floor_contact);
    file=fopen(config,"w");assert(file);fprintf(file,"loopable=1\nloopable=bad\nstartup=2\nis_idle=-1\n");fclose(file);
    assert(anim_config_load(animation,&cfg));
    assert(cfg.has_loopable && !cfg.loopable && cfg.has_startup && !cfg.startup && !cfg.is_idle);
    unlink(config);rmdir(directory);
    puts("Animation config tests passed: behaviour flags, strict booleans, tuning preservation, duplicate updates and long future settings");
    return 0;
}
