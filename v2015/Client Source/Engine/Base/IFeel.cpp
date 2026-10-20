#include "StdH.h"
#include <Engine/Base/IFeel.h>
#include <Engine/Base/FileName.h>
#include <Engine/Base/Shell.h>

// Immersion iFeel force feedback mice used to be driven through ImmWrapper.dll, loaded at run
// time. That library is not shipped and the hardware is long gone, so these are no-ops; the
// shell symbols stay declared because the settings file and scripts may refer to them.

FLOAT ifeel_fGain = 1.0f;
INDEX ifeel_bEnabled = FALSE;

static void ifeel_GainChange(void *ptr)
{
}

CTString IFeel_GetProductName()
{
  return "";
}

CTString IFeel_GetProjectFileName()
{
  return "";
}

BOOL IFeel_InitDevice(HINSTANCE &hInstance, HWND &hWnd)
{
  _pShell->DeclareSymbol("void inp_IFeelGainChange(INDEX);", &ifeel_GainChange);
  _pShell->DeclareSymbol("persistent user FLOAT inp_fIFeelGain post:inp_IFeelGainChange;", &ifeel_fGain);
  _pShell->DeclareSymbol("const user INDEX sys_bIFeelEnabled;", &ifeel_bEnabled);
  return FALSE;
}

void IFeel_DeleteDevice()
{
}

BOOL IFeel_LoadFile(CTFileName fnFile)
{
  return FALSE;
}

void IFeel_UnLoadFile()
{
}

void IFeel_PlayEffect(char *pstrEffectName)
{
}

void IFeel_StopEffect(char *pstrEffectName)
{
}

void IFeel_ChangeGain(FLOAT fGain)
{
}
