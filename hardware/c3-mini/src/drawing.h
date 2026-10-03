#pragma once
// Five-column diagnostic glyphs, digits then A-Z; each bit is one pixel row.
const uint8_t glyphs[][5]={
{62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},{24,20,18,127,16},
{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},{54,73,73,73,54},{6,73,73,41,30},
{126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},{127,73,73,73,65},
{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},{0,65,127,65,0},{32,64,65,63,1},
{127,8,20,34,65},{127,64,64,64,64},{127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},
{127,9,9,9,6},{62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
{63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},{7,8,112,8,7},{97,81,73,69,67}};
uint16_t rgb(uint8_t r,uint8_t g,uint8_t b) {uint16_t c=((r>>3)<<11)|((g>>2)<<5)|(b>>3);return (c>>8)|(c<<8);}
void dot(int x,int y,uint16_t color) {if(x>=0&&x<W&&y>=stripY&&y<stripY+stripHeight&&y<H)pixels[(y-stripY)*W+x]=color;}
void rect(int x,int y,int w,int h,uint16_t c){for(int yy=max(y,stripY);yy<min(y+h,stripY+stripHeight);++yy)for(int xx=max(0,x);xx<min(W,x+w);++xx)dot(xx,yy,c);}
void text(int y,const char *s,uint16_t color,int scale=1) {
    int x=(W-int(strlen(s))*6*scale)/2;
    for(;*s;++s,x+=6*scale){
        char ch=char(toupper(static_cast<unsigned char>(*s)));
        int n=ch>='0'&&ch<='9'?ch-'0':ch>='A'&&ch<='Z'?ch-'A'+10:-1;
        for(int col=0;col<5;++col){uint8_t bits=n>=0?glyphs[n][col]:*s=='-'?8:*s=='+'?(col==2?62:8):*s=='.'?(col==2?64:0):*s=='/'?(64>>col):*s==':'?(col==2?36:0):0;
            for(int row=0;row<7;++row)if(bits&(1<<row))rect(x+col*scale,y+row*scale,scale,scale,color);
        }
    }
}

void line(int x,int y,int xx,int yy,uint16_t color){
    int dx=abs(xx-x),sx=x<xx?1:-1,dy=-abs(yy-y),sy=y<yy?1:-1,err=dx+dy;
    for(;;){dot(x,y,color);if(x==xx&&y==yy)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}
void circle(int x,int y,int r,uint16_t color){
    int xx=r,yy=0,err=1-r;
    while(xx>=yy){dot(x+xx,y+yy,color);dot(x+yy,y+xx,color);dot(x-yy,y+xx,color);dot(x-xx,y+yy,color);dot(x-xx,y-yy,color);dot(x-yy,y-xx,color);dot(x+yy,y-xx,color);dot(x+xx,y-yy,color);++yy;if(err<0)err+=2*yy+1;else{--xx;err+=2*(yy-xx)+1;}}
}
