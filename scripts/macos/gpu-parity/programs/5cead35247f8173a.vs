vs.1.1                                     
mov  r0,      v0                           
mov  r8.xyz,  v1.xyz                       
mov  r7.xyz,  v9.xyz                       
mul  r4.xyz,  r7.zxy,  r8.yzx              
mad  r6.xyz,  r7.yzx,  r8.zxy,  -r4.xyz    
mul  r6.xyz,  r6.xyz,  v9.www              
dp3  r6.w,    r6.xyz,  r6.xyz              
rsq  r6.w,    r6.w                         
mul  r6.xyz,  r6.xyz,  r6.www              
m3x3 r1.xyz,  c4.xyz,  r6                  
m4x4 oPos,    r0,      c0                  
mov  oT0,     v5                           
mov  oT1,     v5                           
mov  oD0,     c5                           
mad  oD1.xyz, r1.xyz,  c7.www,  c7.www     
dp3  r10.w,  r0.xyz,  c18.xyz  
dp3  r11.w,  r0.xyz,  c19.xyz  
add  r9.w,   r10.w,   c20.x    
add  r8.w,   r11.w,   c20.y    
mul  oT2.x,  r9.w,    c20.z 
mul  oT2.y,  r8.w,    c20.w 
