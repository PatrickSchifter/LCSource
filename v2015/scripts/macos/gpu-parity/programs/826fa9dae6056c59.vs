vs.1.1                         
mov  r0,     v0                
mov  r1,     v1                
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
mul  oT1.xy, v5.xy, c12.xy  
