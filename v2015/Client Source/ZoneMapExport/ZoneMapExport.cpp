// Exports the GameServer's zone maps (.sat attributes, .sht heights) from a client world (.wld),
// the way the World Editor does it ("Export terrain attribute map", "Export server height map",
// "Export bsp attribute map" and "Export bsp server height map"), without the editor's UI.
//
//   ZoneMapExport <world.wld> <width> <height> <floor0.sat> <floor0.sht> [<floor1.sat> <floor1.sht> ...]
//
// Paths are relative to the game directory (the parent of Bin\, where this program must live).
// For a field (a world with a terrain) floor 0 starts from the terrain's maps and the brushes
// are drawn over them; every other floor, and every floor of a dungeon, comes from the brushes alone.

#include <boost/function.hpp>  // the engine headers expect it (as Nksp/StdH.h does)
#include <Engine/Engine.h>
#include <Engine/World/World.h>
#include <Engine/Brushes/BrushArchive.h>
#include <Engine/Terrain/Terrain.h>
#include <Engine/Terrain/Terrain.hpp>
#include <Engine/Terrain/TerrainArchive.h>
#include <Engine/Templates/DynamicArray.cpp>
#include <stdio.h>
#include <vector>

// Order in which brush attributes are drawn: the World Editor's default (DlgExportBspAttributeMap.cpp).
static const int _aiAttributePriority[] = { 0, 4, 5, 6, 7, 8, 2, 3, 1 };

static void ExportZone(const CTFileName &fnmWorld, PIX pixWidth, PIX pixHeight, int ctFloors, char **astrFiles)
{
	CWorld woWorld;
	woWorld.Load_t(fnmWorld);

	CTerrain *ptrTerrain = NULL;
	if (woWorld.wo_taTerrains.ta_atrTerrains.Count() > 0) {
		ptrTerrain = &woWorld.wo_taTerrains.ta_atrTerrains[0];
	}
	printf("%s: %s, %d brush polygons\n", (const char *)fnmWorld, ptrTerrain != NULL ? "terrain" : "no terrain",
		woWorld.wo_baBrushes.ba_apbpo.Count());

	if (ptrTerrain != NULL) {
		CTerrainImp *ptr = ptrTerrain->tr_ptrTerrain;
		PIX pixAttrWidth  = (ptr->tr_pixHeightMapWidth - 1) * ptr->tr_slAttributeMapSizeAspect;
		PIX pixAttrHeight = (ptr->tr_pixHeightMapHeight - 1) * ptr->tr_slAttributeMapSizeAspect;
		if (pixAttrWidth != pixWidth || pixAttrHeight != pixHeight
		 || PIX(ptr->tr_vMetricSize(1)) != pixWidth || PIX(ptr->tr_vMetricSize(3)) != pixHeight) {
			// A terrain that does not cover the zone (e.g. a small one inside a dungeon) cannot be the
			// base of the maps: export the zone from its brushes only, as a dungeon.
			printf("  terrain maps are %dx%d, zone is %dx%d: using the brushes only\n",
				pixAttrWidth, pixAttrHeight, pixWidth, pixHeight);
			ptrTerrain = NULL;
		}
	}

	std::vector<int> aiPriority(_aiAttributePriority, _aiAttributePriority + ARRAYCOUNT(_aiAttributePriority));
	for (int iFloor = 0; iFloor < ctFloors; iFloor++) {
		CTFileName fnmAttr = CTString(astrFiles[iFloor * 2]);
		CTFileName fnmHeight = CTString(astrFiles[iFloor * 2 + 1]);
		BOOL bField = ptrTerrain != NULL;
		if (bField && iFloor == 0) {
			// The brush exporters read these back and draw over them.
			TR_ExportAttributeMap_t(ptrTerrain, fnmAttr);
			TR_ExportServerHeightMap_t(ptrTerrain, fnmHeight);
		}
		woWorld.WriteAttributeMap(fnmAttr, bField, iFloor + 1, pixWidth, pixHeight, aiPriority);
		woWorld.WriteSvrHeightMap(fnmHeight, bField, iFloor + 1, pixWidth, pixHeight);
		printf("  floor %d: %s, %s\n", iFloor, (const char *)fnmAttr, (const char *)fnmHeight);
	}
}

static int Run(int argc, char *argv[])
{
	// Load worlds as the World Editor does: no game, network or interface behind the entities.
	_bWorldEditorApp = TRUE;
	SE_InitEngine("ZoneMapExport");

	try {
		ExportZone(CTString(argv[1]), atoi(argv[2]), atoi(argv[3]), (argc - 4) / 2, argv + 4);
	} catch (char *strError) {
		fprintf(stderr, "%s: %s\n", argv[1], strError);
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[])
{
	if (argc < 6 || (argc - 4) % 2 != 0) {
		fprintf(stderr, "usage: %s <world.wld> <width> <height> <floor0.sat> <floor0.sht> [...]\n", argv[0]);
		return 2;
	}

	int iResult = 1;
	CTSTREAM_BEGIN {
		iResult = Run(argc, argv);
	} CTSTREAM_END;

	// No SE_EndEngine(): it would save the shell's persistent symbols, which this program has none of.
	return iResult;
}
