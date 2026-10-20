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
m4x4 oPos,   r0,       c0                  
mov  oT0.xy, v5.xy                         
dp3  r4.w,    r1.xyz,  c4.xyz              
max  r4.w,    r4.w,    c7.x                
mul  r5.xyz,  c5.xyz,  r4.www              
add  r5.xyz,  r5.xyz,  c6.xyz              
mov  r5.w,    c5.w                         
min  oD0,     r5,      c7.yyyy             
dp3  r10.w,  r0.xyz,  c18.xyz  
dp3  r11.w,  r0.xyz,  c19.xyz  
add  r9.w,   r10.w,   c20.x    
add  r8.w,   r11.w,   c20.y    
mul  oT2.x,  r9.w,    c20.z 
mul  oT2.y,  r8.w,    c20.w 
