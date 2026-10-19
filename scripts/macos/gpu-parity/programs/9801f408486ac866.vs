vs.1.1                                     
mul  r3,      v3.zyxw, c4.wwww             
add  r2.x,    v2.z,    v2.y                
add  r2.y,    r2.x,    v2.x                
sub  r2.w,    c7.y,    r2.y                
mov  a0.x,    r3.x                         
m4x3 r0,      v0,      c[a0.x + 21]        
m3x3 r8.xyz,  v1.xyz,  c[a0.x + 21]        
m3x3 r7.xyz,  v9.xyz,  c[a0.x + 21]        
mul  r0.xyz,  r0.xyz,  v2.zzz              
mul  r8.xyz,  r8.xyz,  v2.zzz              
mul  r7.xyz,  r7.xyz,  v2.zzz              
mov  a0.x,    r3.y                         
m4x3 r4,      v0,      c[a0.x + 21]        
m3x3 r11.xyz, v1.xyz,  c[a0.x + 21]        
m3x3 r9.xyz,  v9.xyz,  c[a0.x + 21]        
mad  r0.xyz,  r4.xyz,  v2.yyy,  r0.xyz     
mad  r8.xyz,  r11.xyz, v2.yyy,  r8.xyz     
mad  r7.xyz,  r9.xyz,  v2.yyy,  r7.xyz     
mov  a0.x,    r3.z                         
m4x3 r4,      v0,      c[a0.x + 21]        
m3x3 r11.xyz, v1.xyz,  c[a0.x + 21]        
m3x3 r9.xyz,  v9.xyz,  c[a0.x + 21]        
mad  r0.xyz,  r4.xyz,  v2.xxx,  r0.xyz     
mad  r8.xyz,  r11.xyz, v2.xxx,  r8.xyz     
mad  r7.xyz,  r9.xyz,  v2.xxx,  r7.xyz     
mov  a0.x,    r3.w                         
m4x3 r4,      v0,      c[a0.x + 21]        
m3x3 r11.xyz, v1.xyz,  c[a0.x + 21]        
m3x3 r9.xyz,  v9.xyz,  c[a0.x + 21]        
mad  r0.xyz,  r4.xyz,  r2.www,  r0         
mad  r8.xyz,  r11.xyz, v2.www,  r8.xyz     
mad  r7.xyz,  r9.xyz,  v2.www,  r7.xyz     
mul  r4.xyz,  r7.zxy,  r8.yzx              
mad  r6.xyz,  r7.yzx,  r8.zxy,  r4.xyz     
mul  r6.xyz,  r6.xyz,  v9.www              
dp3  r6.w,    r6.xyz,  r6.xyz              
rsq  r6.w,    r6.w                         
mul  r6.xyz,  r6.xyz,  r6.www              
m3x3 r1.xyz,  c4.xyz,  r6                  
mov  r0.w,    c7.y                         
m4x4 oPos,    r0,      c0                  
mov  oT0,     v5                           
mov  oT1,     v5                           
mov  oD0,     c5                           
mad  oD1.xyz, r1.xyz,  c7.www,  c7.www     
sub  r2.xyz,  c8.xyz,  r0.xyz              
dp3  r2.w,    r2.xyz,  r2.xyz              
rsq  r2.w,    r2.w                         
mad  r2.xyz,  r2.xyz,  r2.www, c4.xyz      
dp3  r2.w,    r2.xyz,  r2.xyz              
rsq  r2.w,    r2.w                         
mul  r2.xyz,  r2.xyz,  r2.www              
dp3  r3.xyz,  r8.xyz,  r2.xyz              
mov  r3.w,    c8.w                         
lit  r3.z,    r3                           
mul  oD1.w,   r3.z,    c9.w                
