#include "gem_menu.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PI 3.14159265358979323846

static bool string(const cJSON *json,const char *key,char *out,size_t size) {
    const cJSON *value=cJSON_GetObjectItemCaseSensitive(json,key);
    if (!cJSON_IsString(value) || strlen(value->valuestring)>=size) return false;
    snprintf(out,size,"%s",value->valuestring);return true;
}
static bool colour(const cJSON *json,const char *key,double out[3]) {
    const cJSON *value=cJSON_GetObjectItemCaseSensitive(json,key);unsigned r,g,b;
    if (!cJSON_IsString(value) || strlen(value->valuestring)!=7 || value->valuestring[0]!='#' ||
        sscanf(value->valuestring+1,"%2x%2x%2x",&r,&g,&b)!=3) return false;
    for (int i=1;i<7;i++) if (!((value->valuestring[i]>='0' && value->valuestring[i]<='9') ||
        (value->valuestring[i]>='a' && value->valuestring[i]<='f') || (value->valuestring[i]>='A' && value->valuestring[i]<='F'))) return false;
    out[0]=r/255.;out[1]=g/255.;out[2]=b/255.;return true;
}
static bool number(const cJSON *json,const char *key,double low,double high,double *out) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(json,key);
    if (!cJSON_IsNumber(v) || !isfinite(v->valuedouble) || v->valuedouble<low || v->valuedouble>high) return false;
    *out=v->valuedouble;return true;
}
static bool flag(const cJSON *json,const char *key,bool *out) {
    const cJSON *v=cJSON_GetObjectItemCaseSensitive(json,key);
    if (!cJSON_IsBool(v))return false;
    *out=cJSON_IsTrue(v);return true;
}
int gem_menu_target(const GemMenu *m,const char *group) {
    for (int i=0;i<m->page_count;i++)if (!strcmp(m->pages[i].group,group))return i;
    return -1;
}
int gem_menu_sibling(const GemMenu *m,int page,int direction) {
    if (page<0 || page>=m->page_count || (direction!=-1 && direction!=1))return -1;
    for (int i=page+direction;i>=0 && i<m->page_count;i+=direction)
        if (!strcmp(m->pages[page].group,m->pages[i].group))return i;
    return -1;
}
bool gem_menu_parse(const cJSON *json,GemMenu *out) {
    GemMenu m={0};double n;
    if (!number(json,"version",1,1,&n) || !number(json,"width",448,448,&n))return false;
    m.width=(int)n;
    if (!number(json,"height",472,472,&n) || !string(json,"context",m.context,sizeof(m.context)))return false;
    m.height=(int)n;
    const cJSON *center=cJSON_GetObjectItemCaseSensitive(json,"center");
    if (!cJSON_IsArray(center) || cJSON_GetArraySize(center)!=2)return false;
    const cJSON *cx=cJSON_GetArrayItem(center,0),*cy=cJSON_GetArrayItem(center,1);
    if (!cJSON_IsNumber(cx)||!cJSON_IsNumber(cy)||cx->valuedouble!=224||cy->valuedouble!=220)return false;
    m.cx=224;m.cy=220;
    const cJSON *p=cJSON_GetObjectItemCaseSensitive(json,"palette");
    if (!colour(p,"primary",m.primary)||!colour(p,"accent",m.accent)||!colour(p,"face",m.face)||
        !colour(p,"shadow",m.shadow)||!colour(p,"ink",m.ink))return false;
    const cJSON *icons=cJSON_GetObjectItemCaseSensitive(json,"icons"),*icon;
    if (!cJSON_IsObject(icons) || cJSON_GetArraySize(icons)>GEM_ICONS)return false;
    cJSON_ArrayForEach(icon,icons) {
        if (!icon->string || strlen(icon->string)>=sizeof(m.icons[0].name) || !cJSON_IsArray(icon) ||
            cJSON_GetArraySize(icon)>GEM_COMMANDS)return false;
        GemIcon *dest=&m.icons[m.icon_count++];snprintf(dest->name,sizeof(dest->name),"%s",icon->string);
        const cJSON *cmd;
        cJSON_ArrayForEach(cmd,icon) {
            const cJSON *op=cJSON_GetArrayItem(cmd,0);
            if (!cJSON_IsArray(cmd)||!cJSON_IsString(op)||strlen(op->valuestring)!=1)return false;
            int count=op->valuestring[0]=='C'?6:op->valuestring[0]=='O'?3:
                      (op->valuestring[0]=='M'||op->valuestring[0]=='L')?2:op->valuestring[0]=='Z'?0:-1;
            if (count<0 || cJSON_GetArraySize(cmd)!=count+1)return false;
            GemCommand *d=&dest->commands[dest->count++];d->op=op->valuestring[0];
            for (int i=0;i<count;i++) {
                const cJSON *v=cJSON_GetArrayItem(cmd,i+1);
                if (!cJSON_IsNumber(v)||!isfinite(v->valuedouble)||v->valuedouble< -32||v->valuedouble>64)return false;
                d->p[i]=v->valuedouble;
            }
            if (d->op=='O' && d->p[2]<=0)return false;
        }
    }
    const cJSON *pages=cJSON_GetObjectItemCaseSensitive(json,"pages"),*page;
    if (!cJSON_IsArray(pages)||cJSON_GetArraySize(pages)<1||cJSON_GetArraySize(pages)>GEM_PAGES)return false;
    cJSON_ArrayForEach(page,pages) {
        int pi=m.page_count++;GemPage *dest=&m.pages[pi];
        if (!string(page,"id",dest->id,sizeof(dest->id))||!string(page,"group",dest->group,sizeof(dest->group)))return false;
        for (int i=0;i<pi;i++)if (!strcmp(m.pages[i].id,dest->id))return false;
        const cJSON *items=cJSON_GetObjectItemCaseSensitive(page,"items"),*item;
        if (!cJSON_IsArray(items)||cJSON_GetArraySize(items)>6)return false;
        cJSON_ArrayForEach(item,items) {
            if (m.count>=GEM_ACTIONS)return false;
            GemAction *a=&m.actions[m.count++];a->page=pi;
            if (!string(item,"id",a->id,sizeof(a->id))||!string(item,"label",a->label,sizeof(a->label))||
                !string(item,"icon",a->icon,sizeof(a->icon))||!string(item,"target",a->target,sizeof(a->target))||
                !number(item,"x",50,m.width-50,&a->x)||!number(item,"y",50,m.height-70,&a->y)||
                !number(item,"radius",30,52,&a->radius)||!flag(item,"enabled",&a->enabled)||
                !flag(item,"checked",&a->checked)||!flag(item,"close",&a->close)||
                !flag(item,"hero",&a->hero)||!flag(item,"danger",&a->danger))return false;
            const cJSON *preview=cJSON_GetObjectItemCaseSensitive(item,"previewPalette");
            if(preview) {
                if(!colour(preview,"primary",a->primary)||!colour(preview,"accent",a->accent)||!colour(preview,"face",a->face))return false;
                a->tinted=true;
            }
            bool found=false;for (int i=0;i<m.icon_count;i++)if (!strcmp(m.icons[i].name,a->icon))found=true;
            if (!found)return false;
            for (int i=0;i<m.count-1;i++)if (!strcmp(m.actions[i].id,a->id))return false;
        }
    }
    for (int i=0;i<m.count;i++)if (m.actions[i].target[0] && gem_menu_target(&m,m.actions[i].target)<0)return false;
    *out=m;return true;
}
void gem_menu_default(GemMenu *m) {
    memset(m,0,sizeof(*m));m->width=448;m->height=472;m->cx=224;m->cy=220;m->page_count=1;
    strcpy(m->pages[0].id,"main:0");strcpy(m->pages[0].group,"main");
    double primary[]={.78,.36,.74},accent[]={.95,.78,.55},face[]={.24,.085,.23},shadow[]={.065,.035,.065},ink[]={1,.97,.93};
    memcpy(m->primary,primary,sizeof(primary));memcpy(m->accent,accent,sizeof(accent));memcpy(m->face,face,sizeof(face));
    memcpy(m->shadow,shadow,sizeof(shadow));memcpy(m->ink,ink,sizeof(ink));
    const char *ids[]={"chat","studio","visibility","quit"},*labels[]={"Talk to me","Studio","Hide avatar","Quit"};
    m->count=4;
    for (int i=0;i<4;i++) {
        GemAction *a=&m->actions[i];strcpy(a->id,ids[i]);strcpy(a->label,labels[i]);
        a->x=224+142*cos(-PI/2+i*PI/2);a->y=220+142*sin(-PI/2+i*PI/2);a->radius=i?42:49;
        a->enabled=a->close=true;a->hero=i==0;a->danger=i==3;
    }
}
int gem_menu_hit(const GemMenu *m,int page,double x,double y) {
    if (fabs(x-m->cx)<63 && y>=m->cy-45 && y<=m->cy+73)return -2;
    for (int i=0;i<m->count;i++) {
        const GemAction *a=&m->actions[i];
        if (a->page==page && fabs(x-a->x)<63 && y>=a->y-a->radius-2 && y<=a->y+a->radius+30)
            return i;
    }
    if (y>=439 && y<=471) {
        if (x>=155 && x<=195 && gem_menu_sibling(m,page,-1)>=0)return -3;
        if (x>=253 && x<=293 && gem_menu_sibling(m,page,1)>=0)return -4;
    }
    return -1;
}
