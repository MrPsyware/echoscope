#pragma once
// Included after the canvas drawing helpers. Clockwise advances an item.
inline void itemRing(int index,int count) {
    if(count<1) return;
    const float segment=360.0f/count,gap=count==1?0:std::min(3.0f,segment*.22f);
    for(int n=0;n<count;++n) {
        const float start=-90+n*segment+gap/2,end=start+segment-gap;
        const int steps=std::max(2,int((end-start)/2));
        for(int i=0;i<steps;++i) {
            const float a=(start+(end-start)*i/steps)*sky::pi/180,b=(start+(end-start)*(i+1)/steps)*sky::pi/180;
            line(233+227*std::cos(a),233+227*std::sin(a),233+227*std::cos(b),233+227*std::sin(b),n==index?green:grid,n==index?3:2);
        }
    }
}
inline void pageArrows(bool multiple) {
    if(!multiple) return;
    text(221,"<",&lv_font_montserrat_28,green,24,42);
    text(221,">",&lv_font_montserrat_28,green,400,42);
}
inline int pageSide(int x,int y) { return y>=125 && y<=335?(x<=100?-1:x>=366?1:0):0; }
