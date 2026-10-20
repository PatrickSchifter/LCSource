-- Zone 0 (old Juno) NPC positions and headings set by hand in game: the user stood on the NPC's
-- spot facing the way it should look and ran /whereami. NPC models face the other way from a player
-- with the same heading (checked in game), so a_pos_r = /whereami heading + 180.
-- Applied after zone0-town-facing.sql. Idempotent.
UPDATE t_npc_regen SET a_pos_x=1152.24, a_pos_z=962.45, a_pos_r=-37.85, a_y_layer=0 WHERE a_index=3663376; -- 253 Animal Trainer (/whereami 142.15)
UPDATE t_npc_regen SET a_pos_x=1158.96, a_pos_z=950.77, a_pos_r=53.14, a_y_layer=0 WHERE a_index=3663343; -- 149 Storekeeper Bianca (/whereami -126.86)
