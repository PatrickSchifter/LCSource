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
dp3  r10.w,  r0.xyz,  c18.xyz  
dp3  r11.w,  r0.xyz,  c19.xyz  
add  r9.w,   r10.w,   c20.x    
add  r8.w,   r11.w,   c20.y    
mul  oT1.x,  r9.w,    c20.z 
mul  oT1.y,  r8.w,    c20.w 
