vs.1.1                                 
mul  r3,     v3.zyxw, c4.wwww          
add  r2.x,   v2.z,    v2.y             
add  r2.y,   r2.x,    v2.x             
sub  r2.w,   c7.y,    r2.y             
mov  a0.x,   r3.x                      
m4x3 r0,     v0, c[a0.x + 21]          
m3x3 r1,     v1, c[a0.x + 21]          
mul  r0.xyz, r0.xyz,  v2.zzz           
mul  r1.xyz, r1.xyz,  v2.zzz           
mov  a0.x,   r3.y                      
m4x3 r4,     v0, c[a0.x + 21]          
m3x3 r5,     v1, c[a0.x + 21]          
mad  r0.xyz, r4.xyz,  v2.yyy, r0.xyz   
mad  r1.xyz, r5.xyz,  v2.yyy, r1.xyz   
mov  a0.x,   r3.z                      
m4x3 r4,     v0, c[a0.x + 21]          
m3x3 r5,     v1, c[a0.x + 21]          
mad  r0.xyz, r4.xyz,  v2.xxx, r0.xyz   
mad  r1.xyz, r5.xyz,  v2.xxx, r1.xyz   
mov  a0.x,   r3.w                      
m4x3 r4,     v0, c[a0.x + 21]          
m3x3 r5,     v1, c[a0.x + 21]          
mad  r0.xyz, r4.xyz,  r2.www, r0       
mad  r1.xyz, r5.xyz,  r2.www, r1       
mov  r0.w,   c7.y                      
dp3  r8.w,   r1,      r1       
rsq  r7.w,   r8.w              
mul  r1,     r1,      r7.wwww  
dp3  r4.w,   r1,      c4       
min  r4.w,   r4.w,    c7.y     
max  r4.w,   r4.w,    c7.x     
mul  r5.xyz, c5.xyz,  r4.www   
add  r5.xyz, r5.xyz,  c6.xyz   
mov  r5.w,   c7.y              
mul  oD0,    r5,      c7.yyyy  
m4x4 oPos, r0,   c0            
mov  oT0.xy, v5.xy             
