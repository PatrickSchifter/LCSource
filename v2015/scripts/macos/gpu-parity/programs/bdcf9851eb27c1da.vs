vs.1.1                         
m4x4 oPos,   v0,   c0          
dp3  r10.w,  v0.xyz,  c18.xyz  
dp3  r11.w,  v0.xyz,  c19.xyz  
add  r9.w,   r10.w,   c20.x    
add  r8.w,   r11.w,   c20.y    
mul  oT0.x,  r9.w,    c20.z    
mul  oT0.y,  r8.w,    c20.w    
