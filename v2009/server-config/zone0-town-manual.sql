-- Zone 0 (old Juno) NPC positions and headings set by hand in game (/whereami and the user's
-- corrections). An NPC with heading r looks where a player with heading r looks.
-- Applied after zone0-town-facing.sql. Idempotent.
UPDATE t_npc_regen SET a_pos_x=1152.24, a_pos_z=962.45, a_pos_r=142.15, a_y_layer=0 WHERE a_index=3663376; -- 253 Animal Trainer (/whereami 142.15)
UPDATE t_npc_regen SET a_pos_x=1158.96, a_pos_z=950.77, a_pos_r=93.14, a_y_layer=0 WHERE a_index=3663343; -- 149 Storekeeper Bianca (53.14, then turned left twice by 20)
UPDATE t_npc_regen SET a_pos_r=-10, a_y_layer=0 WHERE a_index=3663380; -- 408 Estella (Leonor in BR): 80 from zone0-town-facing.sql, turned 90 to the right
UPDATE t_npc_regen SET a_pos_r=90, a_y_layer=0 WHERE a_index=3663838 -75; -- 834 Phoenix Club Master (Ikki in BR):  from zone0-town-facing.sql, turned 90 to the left
