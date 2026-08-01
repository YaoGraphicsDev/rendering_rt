#version 450

layout (push_constant) uniform PushConstants {
    int probeSize;  
} consts;

const int border = 2;

void main() {
	/*  4 texels per probe, padded with 1 border texel on each end. probeSize = 4, probeSize + border = 6
        fragCoordX:	0  1  2  3  4  5  6  7  8  9 10 11 12 13 ...
        content:    O||B  I  I  I  I  B||B  I  I  I  I  B||B ...
        depth:      1  1  0  0  0  0  1  1  0  0  0  0  1  1 ... (texelXY.x / probeWithBorderSide)

		later passes:
		1. probe update pass: clamps full screen depth at 0.5, only write to texels where depth is less, i.e. non-border
		2. copy edge pass:	  clamps full screen depth at 0.5, only write to texels where depth is greater, i.e. border
    */
	if(int(gl_FragCoord.x) % (consts.probeSize + border) < border ||
	   int(gl_FragCoord.y) % (consts.probeSize + border) < border) {
			gl_FragDepth = 1.0;
	} else {
			gl_FragDepth = 0.0;
	}
}