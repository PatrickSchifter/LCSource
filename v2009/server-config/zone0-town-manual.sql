-- Zone 0 (old Juno) NPC positions set by hand in game (user stood on the spot and ran /whereami).
-- a_pos_r is the player heading from /whereami + 180 (NPC models face the other way).
-- Applied after zone0-town-facing.sql. Idempotent.
UPDATE t_npc_regen SET a_pos_x=1152.24, a_pos_z=962.45, a_pos_r=45.52, a_y_layer=0 WHERE a_index=3663376; -- 253 Animal Trainer
UPDATE t_npc_regen SET a_pos_x=1158.96, a_pos_z=950.77, a_pos_r=45.52, a_y_layer=0 WHERE a_index=3663343; -- 149 Storekeeper Bianca (faces the same way as the Animal Trainer)
