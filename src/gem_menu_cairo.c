#include "gem_menu.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PI 3.14159265358979323846

static void colour(cairo_t *c,const double rgb[3],double alpha) {
    cairo_set_source_rgba(c,rgb[0],rgb[1],rgb[2],alpha);
}
static void hex(cairo_t *c,double x,double y,double r) {
    cairo_new_path(c);
    for (int i=0;i<6;i++) {
        double a=-PI/2+i*PI/3,px=x+r*cos(a),py=y+r*sin(a);
        if (i)cairo_line_to(c,px,py);else cairo_move_to(c,px,py);
    }
    cairo_close_path(c);
}
static void pill(cairo_t *c,double x,double y,double w,double h) {
    double r=h/2;cairo_new_sub_path(c);
    cairo_arc(c,x+w-r,y+r,r,-PI/2,PI/2);cairo_arc(c,x+r,y+r,r,PI/2,3*PI/2);cairo_close_path(c);
}
static void text(cairo_t *c,const char *value,double x,double y,double size,double max_width) {
    char buffer[140];snprintf(buffer,sizeof(buffer),"%s",value);
    cairo_set_font_size(c,size);cairo_text_extents_t e;cairo_text_extents(c,buffer,&e);
    while (e.width>max_width && strlen(buffer)>3) {
        size_t n=strlen(buffer);n--;
        while (n && ((unsigned char)buffer[n]&0xc0)==0x80)n--;
        buffer[n]=0;cairo_text_extents(c,buffer,&e);
    }
    cairo_move_to(c,x-e.width/2-e.x_bearing,y);cairo_show_text(c,buffer);
}
static void icon(const GemMenu *m,cairo_t *c,const char *name,double x,double y,double size) {
    const GemIcon *found=NULL;
    for (int i=0;i<m->icon_count;i++)if (!strcmp(m->icons[i].name,name))found=&m->icons[i];
    cairo_save(c);cairo_translate(c,x-size/2,y-size/2);cairo_scale(c,size/28,size/28);cairo_translate(c,2,2);
    cairo_new_path(c);cairo_set_line_width(c,1.7);cairo_set_line_cap(c,CAIRO_LINE_CAP_ROUND);cairo_set_line_join(c,CAIRO_LINE_JOIN_ROUND);
    if (found)for (int i=0;i<found->count;i++) {
        const GemCommand *cmd=&found->commands[i];const double *p=cmd->p;
        switch(cmd->op) {
            case 'M':cairo_move_to(c,p[0],p[1]);break;
            case 'L':cairo_line_to(c,p[0],p[1]);break;
            case 'C':cairo_curve_to(c,p[0],p[1],p[2],p[3],p[4],p[5]);break;
            case 'O':cairo_new_sub_path(c);cairo_arc(c,p[0],p[1],p[2],0,2*PI);break;
            case 'Z':cairo_close_path(c);break;
        }
    }
    else { cairo_arc(c,12,7,4,0,2*PI);cairo_new_sub_path(c);cairo_arc(c,12,23,9,PI,2*PI); }
    colour(c,m->ink,1);cairo_stroke(c);cairo_restore(c);
}
static void gem(const GemMenu *m,cairo_t *c,const GemAction *a,bool hot) {
    double x=a->x,y=a->y,r=a->radius;
    const double *primary=a->tinted?a->primary:m->primary,*accent=a->tinted?a->accent:m->accent,*face=a->tinted?a->face:m->face;
    cairo_push_group(c);
    for (int i=4;i>0;i--) {
        hex(c,x,y,r+i*1.5);colour(c,primary,hot?.07:.035);cairo_set_line_width(c,i*3);cairo_stroke(c);
    }
    hex(c,x,y,r);cairo_pattern_t *gradient=cairo_pattern_create_linear(x,y-r,x,y+r);
    const double danger[]={.71,.30,.43};const double *top=a->danger&&hot?danger:primary;
    cairo_pattern_add_color_stop_rgb(gradient,0,top[0],top[1],top[2]);
    cairo_pattern_add_color_stop_rgb(gradient,1,face[0],face[1],face[2]);
    cairo_set_source(c,gradient);cairo_fill_preserve(c);cairo_pattern_destroy(gradient);
    colour(c,accent,1);cairo_set_line_width(c,hot||a->checked?2.4:1.4);cairo_stroke(c);
    hex(c,x,y,r-5);colour(c,accent,hot?.8:.4);cairo_set_line_width(c,.8);cairo_stroke(c);
    cairo_move_to(c,x,y-r+5);cairo_line_to(c,x,y);cairo_line_to(c,x-r*.77,y-r*.45);cairo_close_path(c);
    cairo_set_source_rgba(c,1,1,1,.10);cairo_fill(c);
    cairo_move_to(c,x,y);cairo_line_to(c,x+r*.77,y+r*.45);cairo_line_to(c,x,y+r-5);cairo_close_path(c);
    cairo_set_source_rgba(c,1,1,1,.06);cairo_fill(c);
    icon(m,c,a->icon,x,y,a->hero?45:37);
    if (a->checked) {
        double bx=x+r*.43+9.5,by=y-r+14.5;
        cairo_arc(c,bx,by,9.5,0,2*PI);colour(c,accent,1);cairo_fill(c);
        cairo_move_to(c,bx-4,by);cairo_line_to(c,bx-1,by+3);cairo_line_to(c,bx+4,by-3);
        colour(c,face,1);cairo_set_line_width(c,1.8);cairo_stroke(c);
    }
    cairo_select_font_face(c,"sans",CAIRO_FONT_SLANT_NORMAL,a->hero?CAIRO_FONT_WEIGHT_BOLD:CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(c,13);cairo_text_extents_t e;cairo_text_extents(c,a->label,&e);
    double width=fmin(126,e.width+18);
    pill(c,x-width/2,y+r+3,width,24);colour(c,m->shadow,.98);cairo_fill_preserve(c);
    if(hot)colour(c,accent,1);else cairo_set_source_rgba(c,.29,.23,.35,.33);
    cairo_set_line_width(c,1);cairo_stroke(c);colour(c,m->ink,1);text(c,a->label,x,y+r+19,13,width-16);
    cairo_pop_group_to_source(c);cairo_paint_with_alpha(c,a->enabled?1:.42);
}
void gem_menu_draw(const GemMenu *m,cairo_t *c,int page,int hover,double opening) {
    if (page<0 || page>=m->page_count)return;
    opening=fmax(0,fmin(1,opening));cairo_save(c);cairo_push_group(c);
    double scale=.92+.08*opening;
    cairo_translate(c,m->width/2.,m->height/2.);cairo_scale(c,scale,scale);cairo_translate(c,-m->width/2.,-m->height/2.);
    cairo_pattern_t *halo=cairo_pattern_create_radial(m->cx,m->cy,30,m->cx,m->cy,188);
    cairo_pattern_add_color_stop_rgba(halo,0,.12,.08,.19,.2);cairo_pattern_add_color_stop_rgba(halo,1,.12,.08,.19,0);
    cairo_set_source(c,halo);cairo_paint(c);cairo_pattern_destroy(halo);
    const double radii[]={129,137,148},alpha[]={.27,.65,.25};
    for (int i=0;i<3;i++) {
        cairo_new_path(c);cairo_arc(c,m->cx,m->cy,radii[i],0,2*PI);colour(c,m->accent,alpha[i]);cairo_set_line_width(c,i==1?2:1);cairo_stroke(c);
    }
    for (int i=0;i<m->count;i++) {
        const GemAction *a=&m->actions[i];if(a->page!=page)continue;
        double dx=a->x-m->cx,dy=a->y-m->cy,d=hypot(dx,dy);if(d<1)continue;
        cairo_move_to(c,m->cx+dx*52/d,m->cy+dy*52/d);cairo_line_to(c,a->x-dx*(a->radius+5)/d,a->y-dy*(a->radius+5)/d);
        colour(c,m->accent,.55);cairo_set_line_width(c,1);cairo_stroke(c);
    }
    const double stars[][3]={{224,49,4},{363,235,3},{85,235,3},{286,103,3},{163,337,3}};
    for (int i=0;i<5;i++) {
        double x=stars[i][0],y=stars[i][1],r=stars[i][2];
        cairo_move_to(c,x-r,y);cairo_line_to(c,x+r,y);cairo_move_to(c,x,y-r);cairo_line_to(c,x,y+r);
        colour(c,m->accent,.8);cairo_stroke(c);
    }
    for (int i=0;i<m->count;i++)if(m->actions[i].page==page)gem(m,c,&m->actions[i],hover==i);
    GemAction core={.x=m->cx,.y=m->cy,.radius=43,.enabled=true};
    bool main=!strcmp(m->pages[page].group,"main");strcpy(core.label,main?"Close":"Back");strcpy(core.icon,main?"avatar":"back");
    gem(m,c,&core,hover==-2);
    int prev=gem_menu_sibling(m,page,-1),next=gem_menu_sibling(m,page,1);
    if(prev>=0||next>=0) {
        int index=1,total=0;
        for(int i=0;i<m->page_count;i++)if(!strcmp(m->pages[i].group,m->pages[page].group)){total++;if(i<page)index++;}
        for(int i=0;i<2;i++) {
            double x=i?253:155;pill(c,x,439,40,28);colour(c,m->shadow,1);cairo_fill_preserve(c);
            colour(c,m->accent,(i?next:prev)>=0?.9:.25);cairo_set_line_width(c,1);cairo_stroke(c);
            text(c,i?"›":"‹",x+20,458,18,36);
        }
        char label[32];snprintf(label,sizeof(label),"%d / %d",index,total);colour(c,m->ink,1);text(c,label,224,458,12,50);
    }
    cairo_pop_group_to_source(c);cairo_paint_with_alpha(c,fmin(1,opening*1.5));cairo_restore(c);
}
