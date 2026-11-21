// 50F900 compares full-width coordinates, then packs x/y to signed shorts.
// Change only horizontal visibility. Keep the engine's vertical aggregate,
// including for faces whose vertices span both vertical sides at a low angle.
static uint16_t stage_outcode(int x,int y,int left,int right,int top,int bottom,uint16_t flags){
    // An explicit side that contradicts the stored coordinate indicates wrap.
    // Bit 2 makes 50FDF0 reject the face even if only one vertex is invalid.
    if(((flags&0x10)&&x>=right)||((flags&0x20)&&x<=left)||
       ((flags&0x40)&&y>=bottom)||((flags&0x80)&&y<=top))return flags|4;
    flags &= ~uint16_t(0x130);
    if(x<left) flags|=0x10;
    if(x>right) flags|=0x20;
    return flags;
}
