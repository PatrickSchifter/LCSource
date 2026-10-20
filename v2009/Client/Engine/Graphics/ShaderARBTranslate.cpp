// No engine headers: the ARB text in, MSL or GLSL out (also built by the offline tests of the
// translation).
#include "ShaderARBToMSL.h"
#include "ShaderARBToGLSL.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <vector>

// Statement by statement: ARB registers keep their names as float4 (vec4) variables (r0.., s0..,
// t0.., k0..), program parameters are the constants block K, inputs are read once at the start
// (vertex streams) or come from the stage input, and an instruction is an assignment to the
// written components of its destination. Scalar instructions replicate their result, as ARB does.
// The two languages differ in their words only (the Dialect below).

namespace {

typedef std::string Str;

static const char *_strHelpersMSL =
	"static inline float4 ARB_LIT(float4 a) {\n"
	"  const float x = max(a.x, 0.0f), y = max(a.y, 0.0f), w = clamp(a.w, -128.0f, 128.0f);\n"
	"  return float4(1.0f, x, x > 0.0f ? (y > 0.0f ? pow(y, w) : (w == 0.0f ? 1.0f : 0.0f)) : 0.0f, 1.0f);\n"
	"}\n"
	"static inline float4 ARB_EXP(float x) {\n"
	"  const float f = floor(x);\n"
	"  return float4(exp2(f), x - f, exp2(x), 1.0f);\n"
	"}\n"
	"static inline float4 ARB_LOG(float x) {\n"
	"  const float a = abs(x), l = log2(a), f = floor(l);\n"
	"  return float4(f, a * exp2(-f), l, 1.0f);\n"
	"}\n";

static const char *_strHelpersGLSL =
	"vec4 ARB_LIT(vec4 a) {\n"
	"  const float x = max(a.x, 0.0f), y = max(a.y, 0.0f), w = clamp(a.w, -128.0f, 128.0f);\n"
	"  return vec4(1.0f, x, x > 0.0f ? (y > 0.0f ? pow(y, w) : (w == 0.0f ? 1.0f : 0.0f)) : 0.0f, 1.0f);\n"
	"}\n"
	"vec4 ARB_EXP(float x) {\n"
	"  const float f = floor(x);\n"
	"  return vec4(exp2(f), x - f, exp2(x), 1.0f);\n"
	"}\n"
	"vec4 ARB_LOG(float x) {\n"
	"  const float a = abs(x), l = log2(a), f = floor(l);\n"
	"  return vec4(f, a * exp2(-f), l, 1.0f);\n"
	"}\n";

// the words of a language
struct Dialect {
	bool d_bGLSL;
	const char *d_strVec4;       // the type of the registers
	const char *d_strOutPos;     // the position the vertex stage writes (the rest is "o.")
	const char *d_strIn;         // the fragment stage input ("<in>col0")
};
static const Dialect _dMSL  = { false, "float4", "o.pos", "in." };
static const Dialect _dGLSL = { true,  "vec4", "gl_Position", "vin." };

static Str Trim(const Str &str)
{
	size_t i = 0, j = str.size();
	while( i<j && isspace((unsigned char)str[i])) i++;
	while( j>i && isspace((unsigned char)str[j-1])) j--;
	return str.substr(i, j-i);
}

static bool IsSwizzle(const Str &str)
{
	if( str.empty() || str.size()>4) return false;
	for( size_t i=0; i<str.size(); i++) if( strchr("xyzw", str[i])==NULL) return false;
	return true;
}

// split at commas outside braces and brackets
static std::vector<Str> SplitOperands(const Str &str)
{
	std::vector<Str> astr;
	int iDepth = 0;
	Str strCur;
	for( size_t i=0; i<str.size(); i++) {
		const char ch = str[i];
		if( ch=='[' || ch=='{') iDepth++;
		if( ch==']' || ch=='}') iDepth--;
		if( ch==',' && iDepth==0) { astr.push_back(Trim(strCur));  strCur = "";  continue; }
		strCur += ch;
	}
	if( !Trim(strCur).empty()) astr.push_back(Trim(strCur));
	return astr;
}


class Translator {
public:
	Translator(const Dialect &d) : tr_d(d), tr_bVertex(false), tr_bAddress(false), tr_ulInputs(0) {}
	bool Translate(const Str &strARB, Str &strOut, Str &strError);

private:
	const Dialect &tr_d;
	bool tr_bVertex;
	bool tr_bAddress;
	unsigned int tr_ulInputs;   // vertex inputs read (bits of the IN_* below)
	std::set<int> tr_setTextures;
	Str tr_strError;
	std::vector<Str> tr_astrDecl;
	std::vector<Str> tr_astrBody;

	enum { IN_POS=1, IN_NOR=2, IN_COL=4, IN_TEX0=8, IN_WGH=128, IN_IDX=256, IN_TAN=512 };

	bool Fail(const Str &str) { if( tr_strError.empty()) tr_strError = str; return false; }
	bool Base(const Str &strBase, bool bDest, Str &strOut);
	bool Source(const Str &strOperand, Str &strOut);
	bool Dest(const Str &strOperand, Str &strBase, Str &strMask);
	bool Instruction(const Str &strOp, const std::vector<Str> &astrOperands);
	bool Declaration(const Str &strStatement);
	void FinishMSL(Str &strOut);
	void FinishGLSL(Str &strOut);
};


// a register of the ARB program as a float4 lvalue or value
bool Translator::Base(const Str &strBase, bool bDest, Str &strOut)
{
	char str[64];
	int i;
	// temporaries and constants of the program keep their names
	if( !strBase.empty() && strchr("rstk", strBase[0]) && strBase.size()>1 && isdigit((unsigned char)strBase[1])) {
		strOut = strBase;
		return true;
	}
	if( strBase.compare(0, 4, "cdef")==0) { strOut = strBase;  return true; }

	if( bDest) {
		if( tr_bVertex) {
			if( strBase=="result.position")        strOut = tr_d.d_strOutPos;
			else if( strBase=="result.color")      strOut = "o.col0";
			else if( strBase=="result.color.secondary") strOut = "o.col1";
			else if( strBase=="result.fogcoord" || strBase=="result.pointsize") strOut = "o_unused";
			else if( sscanf(strBase.c_str(), "result.texcoord[%d]", &i)==1 && i>=0 && i<4) {
				snprintf(str, sizeof(str), "o.tex%d", i);  strOut = str;
			} else return Fail("cannot write " + strBase);
		} else {
			if( strBase=="result.color") strOut = "o_col";
			else return Fail("cannot write " + strBase);
		}
		return true;
	}

	// program parameters
	if( strBase.compare(0, 2, "c[")==0) {
		const Str strInner = strBase.substr(2, strBase.size()-3);
		if( strInner.compare(0, 4, "a0.x")==0) {
			const int iOffset = atoi(strInner.c_str()+4);  // "+21" or "-3"
			snprintf(str, sizeof(str), "K.c[clamp(a0 + %d, 0, %d)]", iOffset, GPU_VERTEX_CONSTANTS-1);
		} else {
			snprintf(str, sizeof(str), "K.c[%d]", atoi(strInner.c_str()));
		}
		strOut = str;
		return true;
	}
	if( sscanf(strBase.c_str(), "program.env[%d]", &i)==1) {
		if( i<0 || i>=(tr_bVertex ? GPU_VERTEX_CONSTANTS : GPU_PIXEL_CONSTANTS)) return Fail("no such parameter " + strBase);
		snprintf(str, sizeof(str), "K.c[%d]", i);  strOut = str;
		return true;
	}

	if( tr_bVertex) {
		if( strBase=="vertex.position")     { strOut = "v_pos";  tr_ulInputs |= IN_POS; }
		else if( strBase=="vertex.normal")  { strOut = "v_nor";  tr_ulInputs |= IN_NOR; }
		else if( strBase=="vertex.color")   { strOut = "v_col";  tr_ulInputs |= IN_COL; }
		else if( sscanf(strBase.c_str(), "vertex.texcoord[%d]", &i)==1 && i>=0 && i<4) {
			snprintf(str, sizeof(str), "v_tex%d", i);  strOut = str;  tr_ulInputs |= IN_TEX0<<i;
		} else if( sscanf(strBase.c_str(), "vertex.attrib[%d]", &i)==1) {
			// ShaderAsmToARB.h: 1 tangent, 6 weights, 7 bone indices
			if( i==1)      { strOut = "v_tan";  tr_ulInputs |= IN_TAN; }
			else if( i==6) { strOut = "v_wgh";  tr_ulInputs |= IN_WGH; }
			else if( i==7) { strOut = "v_idx";  tr_ulInputs |= IN_IDX; }
			else return Fail("no such attribute " + strBase);
		} else return Fail("cannot read " + strBase);
	} else {
		if( strBase=="fragment.color.primary" || strBase=="fragment.color") strOut = Str(tr_d.d_strIn) + "col0";
		else if( strBase=="fragment.color.secondary") strOut = Str(tr_d.d_strIn) + "col1";
		else if( sscanf(strBase.c_str(), "fragment.texcoord[%d]", &i)==1 && i>=0 && i<4) {
			snprintf(str, sizeof(str), "%stex%d", tr_d.d_strIn, i);  strOut = str;
		} else return Fail("cannot read " + strBase);
	}
	return true;
}


// a source operand: [-]register[.swizzle], as a float4 expression
bool Translator::Source(const Str &strOperand, Str &strOut)
{
	Str str = strOperand;
	bool bNeg = false;
	if( !str.empty() && str[0]=='-') { bNeg = true;  str = Trim(str.substr(1)); }
	Str strSwz;
	const size_t iDot = str.rfind('.');
	if( iDot!=Str::npos && IsSwizzle(str.substr(iDot+1)) && str.compare(0, 4, "a0.x")!=0) {
		strSwz = str.substr(iDot+1);
		str = str.substr(0, iDot);
	}
	Str strBase;
	if( !Base(str, false, strBase)) return false;
	if( !strSwz.empty() && strSwz!="xyzw") {
		while( strSwz.size()<4) strSwz += strSwz[strSwz.size()-1];
		strBase += "." + strSwz;
	}
	strOut = bNeg ? "(-" + strBase + ")" : strBase;
	return true;
}


bool Translator::Dest(const Str &strOperand, Str &strBase, Str &strMask)
{
	Str str = strOperand;
	strMask = "";
	const size_t iDot = str.rfind('.');
	if( iDot!=Str::npos && IsSwizzle(str.substr(iDot+1))) {
		strMask = str.substr(iDot+1);
		str = str.substr(0, iDot);
	}
	if( strMask=="xyzw") strMask = "";
	return Base(str, true, strBase);
}


bool Translator::Instruction(const Str &strOpFull, const std::vector<Str> &astrOp)
{
	Str strOp = strOpFull;
	bool bSat = false;
	if( strOp.size()>4 && strOp.compare(strOp.size()-4, 4, "_SAT")==0) {
		bSat = true;
		strOp.erase(strOp.size()-4);
	}

	if( strOp=="ARL") {
		if( astrOp.size()!=2) return Fail("bad ARL");
		Str strSrc;
		if( !Source(astrOp[1], strSrc)) return false;
		tr_astrBody.push_back("a0 = int(floor((" + strSrc + ").x));");
		return true;
	}
	if( strOp=="KIL") {
		if( astrOp.size()!=1) return Fail("bad KIL");
		Str strSrc;
		if( !Source(astrOp[0], strSrc)) return false;
		if( tr_d.d_bGLSL) tr_astrBody.push_back("if (any(lessThan(" + strSrc + ", vec4(0.0f)))) discard;");
		else tr_astrBody.push_back("if (any(" + strSrc + " < 0.0f)) discard_fragment();");
		return true;
	}

	struct OpInfo { const char *strARB; int ctSources; const char *strMSL; const char *strGLSL; };
	static const OpInfo aoi[] = {
		{"MOV",1,"%0","%0"}, {"ADD",2,"(%0 + %1)","(%0 + %1)"}, {"SUB",2,"(%0 - %1)","(%0 - %1)"},
		{"MUL",2,"(%0 * %1)","(%0 * %1)"}, {"MAD",3,"(%0 * %1 + %2)","(%0 * %1 + %2)"},
		{"DP3",2,"float4(dot((%0).xyz, (%1).xyz))","vec4(dot((%0).xyz, (%1).xyz))"},
		{"DP4",2,"float4(dot(%0, %1))","vec4(dot(%0, %1))"},
		{"DPH",2,"float4(dot((%0).xyz, (%1).xyz) + (%1).w)","vec4(dot((%0).xyz, (%1).xyz) + (%1).w)"},
		{"MIN",2,"min(%0, %1)","min(%0, %1)"}, {"MAX",2,"max(%0, %1)","max(%0, %1)"},
		{"SLT",2,"select(float4(0.0f), float4(1.0f), %0 < %1)","mix(vec4(0.0f), vec4(1.0f), lessThan(%0, %1))"},
		{"SGE",2,"select(float4(0.0f), float4(1.0f), %0 >= %1)","mix(vec4(0.0f), vec4(1.0f), greaterThanEqual(%0, %1))"},
		{"FRC",1,"fract(%0)","fract(%0)"}, {"FLR",1,"floor(%0)","floor(%0)"}, {"ABS",1,"abs(%0)","abs(%0)"},
		{"LIT",1,"ARB_LIT(%0)","ARB_LIT(%0)"},
		{"DST",2,"float4(1.0f, (%0).y*(%1).y, (%0).z, (%1).w)","vec4(1.0f, (%0).y*(%1).y, (%0).z, (%1).w)"},
		{"RCP",1,"float4(1.0f/(%0).x)","vec4(1.0f/(%0).x)"},
		{"RSQ",1,"float4(rsqrt(abs((%0).x)))","vec4(inversesqrt(abs((%0).x)))"},
		{"EX2",1,"float4(exp2((%0).x))","vec4(exp2((%0).x))"},
		{"LG2",1,"float4(log2(abs((%0).x)))","vec4(log2(abs((%0).x)))"},
		{"POW",2,"float4(pow((%0).x, (%1).x))","vec4(pow((%0).x, (%1).x))"},
		{"EXP",1,"ARB_EXP((%0).x)","ARB_EXP((%0).x)"}, {"LOG",1,"ARB_LOG((%0).x)","ARB_LOG((%0).x)"},
		{"LRP",3,"mix(%2, %1, %0)","mix(%2, %1, %0)"},
		{"CMP",3,"select(%2, %1, %0 < 0.0f)","mix(%2, %1, lessThan(%0, vec4(0.0f)))"},
		{"TEX",3,"",""},
	};
	for( size_t iOp=0; iOp<sizeof(aoi)/sizeof(aoi[0]); iOp++) {
		const OpInfo &oi = aoi[iOp];
		if( strOp!=oi.strARB) continue;
		const size_t ctOperands = (strOp=="TEX") ? 4 : (size_t)oi.ctSources+1;
		if( astrOp.size()!=ctOperands) return Fail("wrong operand count for " + strOp);
		Str strDest, strMask;
		if( !Dest(astrOp[0], strDest, strMask)) return false;

		Str strExpr;
		if( strOp=="TEX") {
			// TEX d, coord, texture[n], 2D
			int iUnit = -1;
			if( sscanf(astrOp[2].c_str(), "texture[%d]", &iUnit)!=1 || iUnit<0 || iUnit>3 || astrOp[3]!="2D") return Fail("bad TEX");
			Str strCoord;
			if( !Source(astrOp[1], strCoord)) return false;
			char str[64];
			if( tr_d.d_bGLSL) {
				snprintf(str, sizeof(str), "texture(tex%d, ", iUnit);
				strExpr = Str(str) + "(" + strCoord + ").xy, K.fLODBias)";
			} else {
				snprintf(str, sizeof(str), "tex%d.sample(smp%d, ", iUnit, iUnit);
				strExpr = Str(str) + "(" + strCoord + ").xy, bias(K.fLODBias))";
			}
			tr_setTextures.insert(iUnit);
		} else {
			std::vector<Str> astrSrc(oi.ctSources);
			for( int iSrc=0; iSrc<oi.ctSources; iSrc++) {
				if( !Source(astrOp[iSrc+1], astrSrc[iSrc])) return false;
			}
			for( const char *pch=(tr_d.d_bGLSL ? oi.strGLSL : oi.strMSL); *pch!=0; pch++) {
				if( pch[0]=='%' && isdigit((unsigned char)pch[1])) { strExpr += astrSrc[pch[1]-'0'];  pch++; }
				else strExpr += *pch;
			}
		}
		if( bSat) strExpr = tr_d.d_bGLSL ? "clamp(" + strExpr + ", 0.0f, 1.0f)" : "saturate(" + strExpr + ")";
		if( strMask.empty()) tr_astrBody.push_back(strDest + " = " + strExpr + ";");
		else tr_astrBody.push_back(strDest + "." + strMask + " = (" + strExpr + ")." + strMask + ";");
		return true;
	}
	return Fail("unknown instruction " + strOpFull);
}


bool Translator::Declaration(const Str &strStatement)
{
	const Str strVec4 = tr_d.d_strVec4;
	if( strStatement.compare(0, 6, "PARAM ")==0) {
		const size_t iEq = strStatement.find('=');
		if( iEq==Str::npos) return Fail("bad PARAM");
		const Str strName = Trim(strStatement.substr(6, iEq-6));
		const Str strValue = Trim(strStatement.substr(iEq+1));
		// the vertex constants array: K.c
		if( strName.compare(0, 2, "c[")==0) return true;
		if( strValue.size()<2 || strValue[0]!='{' || strValue[strValue.size()-1]!='}') return Fail("bad PARAM " + strName);
		std::vector<Str> astr = SplitOperands(strValue.substr(1, strValue.size()-2));
		if( astr.size()!=4) return Fail("PARAM " + strName + " without four values");
		Str strDecl = "const " + strVec4 + " " + strName + " = " + strVec4 + "(";
		for( size_t i=0; i<4; i++) {
			Str strNum = astr[i];
			// a float literal: "1" -> "1.0f" (as written by ShaderAsmToARB, or by a def)
			if( !strNum.empty() && (strNum[strNum.size()-1]=='f' || strNum[strNum.size()-1]=='F')) strNum.erase(strNum.size()-1);
			if( strNum.find_first_of(".eE")==Str::npos) strNum += ".0";
			strDecl += (i ? ", " : "") + strNum + "f";
		}
		tr_astrDecl.push_back(strDecl + ");");
		return true;
	}
	if( strStatement.compare(0, 5, "TEMP ")==0) {
		std::vector<Str> astr = SplitOperands(strStatement.substr(5));
		Str strDecl = strVec4 + " ";
		for( size_t i=0; i<astr.size(); i++) strDecl += (i ? ", " : "") + astr[i] + " = " + strVec4 + "(0.0f)";
		tr_astrDecl.push_back(strDecl + ";");
		return true;
	}
	if( strStatement=="ADDRESS a0") {
		tr_bAddress = true;
		return true;
	}
	return false;  // not a declaration
}


bool Translator::Translate(const Str &strARB, Str &strOut, Str &strError)
{
	// statements end with ';' (the header line and END have none)
	size_t iStart = 0;
	bool bHeader = true;
	while( iStart<strARB.size()) {
		size_t iEnd = strARB.find_first_of(bHeader ? "\n" : ";\n", iStart);
		if( iEnd==Str::npos) iEnd = strARB.size();
		const Str strStatement = Trim(strARB.substr(iStart, iEnd-iStart));
		iStart = iEnd+1;
		if( strStatement.empty()) continue;
		if( bHeader) {
			if( strStatement=="!!ARBvp1.0") tr_bVertex = true;
			else if( strStatement=="!!ARBfp1.0") tr_bVertex = false;
			else { strError = "not an ARB program"; return false; }
			bHeader = false;
			continue;
		}
		if( strStatement=="END") break;
		if( Declaration(strStatement)) continue;
		if( !tr_strError.empty()) { strError = tr_strError; return false; }

		size_t i = 0;
		while( i<strStatement.size() && !isspace((unsigned char)strStatement[i])) i++;
		const Str strOp = strStatement.substr(0, i);
		if( !Instruction(strOp, SplitOperands(strStatement.substr(i)))) {
			strError = tr_strError + " in: " + strStatement;
			return false;
		}
	}
	if( bHeader) { strError = "empty program"; return false; }

	if( tr_d.d_bGLSL) FinishGLSL(strOut);
	else FinishMSL(strOut);
	return true;
}


void Translator::FinishMSL(Str &strMSL)
{
	strMSL = MSL_PROGRAM_INTERFACE;
	char str[256];
	if( tr_bVertex) {
		snprintf(str, sizeof(str),
			"struct VPConst { float4 c[%d]; float4 colConstant; uint ulStreams; uint ulTexCoords; uint2 ulPad; };\n",
			GPU_VERTEX_CONSTANTS);
		strMSL += str;
	} else {
		snprintf(str, sizeof(str),
			"struct PPConst { float4 c[%d]; float fAlphaRef; uint bAlphaTest; float fLODBias; uint ulPad; };\n",
			GPU_PIXEL_CONSTANTS);
		strMSL += str;
	}
	strMSL += _strHelpersMSL;

	if( tr_bVertex) {
		// streams as the fixed function emulation reads them (GpuStream + 1)
		strMSL +=
			"vertex MtlVSOut vp(uint vid [[vertex_id]], constant VPConst &K [[buffer(0)]],\n"
			"  const device packed_float3 *pvtx [[buffer(1)]], const device uchar4 *pcol [[buffer(2)]],\n"
			"  const device packed_float3 *pnor [[buffer(3)]],\n"
			"  const device float *ptex0 [[buffer(4)]], const device float *ptex1 [[buffer(5)]],\n"
			"  const device float *ptex2 [[buffer(6)]], const device float *ptex3 [[buffer(7)]],\n"
			"  const device uchar4 *pwgh [[buffer(8)]], const device packed_float4 *ptan [[buffer(9)]])\n"
			"{\n"
			"  MtlVSOut o;\n"
			"  o.pos = float4(0.0f);  o.col0 = float4(0.0f);  o.col1 = float4(0.0f);\n"
			"  o.tex0 = o.tex1 = o.tex2 = o.tex3 = float4(0.0f, 0.0f, 0.0f, 1.0f);\n"
			"  o.fPointSize = 1.0f;  o.afClip[0] = 1.0f;\n"
			"  float4 o_unused;\n";
		// inputs: what OpenGL gives a vertex program for an array, or without one
		if( tr_ulInputs&IN_POS) strMSL += "  const float4 v_pos = float4(float3(pvtx[vid]), 1.0f);\n";
		if( tr_ulInputs&IN_NOR) strMSL += "  const float4 v_nor = (K.ulStreams & 4) ? float4(float3(pnor[vid]), 1.0f) : float4(0.0f, 0.0f, 1.0f, 1.0f);\n";
		if( tr_ulInputs&IN_COL) strMSL += "  const float4 v_col = (K.ulStreams & 2) ? float4(pcol[vid])/255.0f : K.colConstant;\n";
		for( int iUnit=0; iUnit<4; iUnit++) {
			if( !(tr_ulInputs&(IN_TEX0<<iUnit))) continue;
			snprintf(str, sizeof(str),
				"  const float4 v_tex%d = !(K.ulStreams & %d) ? float4(0.0f, 0.0f, 0.0f, 1.0f)\n"
				"    : ((K.ulTexCoords >> %d) & 2) ? float4(ptex%d[vid*4], ptex%d[vid*4+1], ptex%d[vid*4+2], ptex%d[vid*4+3])\n"
				"    : float4(ptex%d[vid*2], ptex%d[vid*2+1], 0.0f, 1.0f);\n",
				iUnit, 1<<(3+iUnit), iUnit*4, iUnit, iUnit, iUnit, iUnit, iUnit, iUnit);
			strMSL += str;
		}
		// GFXWeight: four bone indices, then four weights (bytes, normalized as OpenGL reads them)
		if( tr_ulInputs&IN_IDX) strMSL += "  const float4 v_idx = (K.ulStreams & 128) ? float4(pwgh[vid*2])/255.0f : float4(0.0f, 0.0f, 0.0f, 1.0f);\n";
		if( tr_ulInputs&IN_WGH) strMSL += "  const float4 v_wgh = (K.ulStreams & 128) ? float4(pwgh[vid*2+1])/255.0f : float4(0.0f, 0.0f, 0.0f, 1.0f);\n";
		if( tr_ulInputs&IN_TAN) strMSL += "  const float4 v_tan = (K.ulStreams & 256) ? float4(ptan[vid]) : float4(0.0f, 0.0f, 0.0f, 1.0f);\n";
	} else {
		strMSL +=
			"fragment float4 pp(MtlPSIn in [[stage_in]], constant PPConst &K [[buffer(0)]],\n"
			"  texture2d<float> tex0 [[texture(0)]], sampler smp0 [[sampler(0)]],\n"
			"  texture2d<float> tex1 [[texture(1)]], sampler smp1 [[sampler(1)]],\n"
			"  texture2d<float> tex2 [[texture(2)]], sampler smp2 [[sampler(2)]],\n"
			"  texture2d<float> tex3 [[texture(3)]], sampler smp3 [[sampler(3)]])\n"
			"{\n"
			"  float4 o_col = float4(0.0f);\n";
	}
	if( tr_bAddress) strMSL += "  int a0 = 0;\n";
	for( size_t i=0; i<tr_astrDecl.size(); i++) strMSL += "  " + tr_astrDecl[i] + "\n";
	for( size_t i=0; i<tr_astrBody.size(); i++) strMSL += "  " + tr_astrBody[i] + "\n";
	if( tr_bVertex) {
		strMSL += "  return o;\n}\n";
	} else {
		// OpenGL's alpha test comes after the program
		strMSL += "  if (K.bAlphaTest != 0 && o_col.a < K.fAlphaRef) discard_fragment();\n";
		strMSL += "  return o_col;\n}\n";
	}
}


void Translator::FinishGLSL(Str &strGLSL)
{
	char str[512];
	if( tr_bVertex) {
		strGLSL = GLSL_VERTEX_INTERFACE;
		snprintf(str, sizeof(str),
			"layout(set = 0, binding = 0, std140) uniform VPConst { vec4 c[%d]; vec4 colConstant; uint ulStreams; uint ulTexCoords; uvec2 ulPad; } K;\n",
			GPU_VERTEX_CONSTANTS);
	} else {
		strGLSL = GLSL_FRAGMENT_INTERFACE;
		snprintf(str, sizeof(str),
			"layout(set = 0, binding = 1, std140) uniform PPConst { vec4 c[%d]; float fAlphaRef; uint bAlphaTest; float fLODBias; uint ulPad; } K;\n",
			GPU_PIXEL_CONSTANTS);
	}
	strGLSL += str;
	strGLSL += _strHelpersGLSL;

	if( tr_bVertex) {
		strGLSL +=
			"void main()\n"
			"{\n"
			"  const uint vid = uint(gl_VertexIndex);\n"
			"  gl_Position = vec4(0.0f);  o.col0 = vec4(0.0f);  o.col1 = vec4(0.0f);\n"
			"  o.tex0 = o.tex1 = o.tex2 = o.tex3 = vec4(0.0f, 0.0f, 0.0f, 1.0f);\n"
			"  gl_PointSize = 1.0f;  gl_ClipDistance[0] = 1.0f;\n"
			"  vec4 o_unused;\n";
		// inputs: what OpenGL gives a vertex program for an array, or without one
		if( tr_ulInputs&IN_POS) strGLSL += "  const vec4 v_pos = vec4(GpuPos(vid), 1.0f);\n";
		if( tr_ulInputs&IN_NOR) strGLSL += "  const vec4 v_nor = (K.ulStreams & 4u) != 0u ? vec4(GpuNor(vid), 1.0f) : vec4(0.0f, 0.0f, 1.0f, 1.0f);\n";
		if( tr_ulInputs&IN_COL) strGLSL += "  const vec4 v_col = (K.ulStreams & 2u) != 0u ? GpuCol(vid) : K.colConstant;\n";
		for( int iUnit=0; iUnit<4; iUnit++) {
			if( !(tr_ulInputs&(IN_TEX0<<iUnit))) continue;
			snprintf(str, sizeof(str),
				"  const vec4 v_tex%d = (K.ulStreams & %du) == 0u ? vec4(0.0f, 0.0f, 0.0f, 1.0f)\n"
				"    : GpuTex%d(vid, ((K.ulTexCoords >> %du) & 2u) != 0u);\n",
				iUnit, 1<<(3+iUnit), iUnit, iUnit*4);
			strGLSL += str;
		}
		// GFXWeight: four bone indices, then four weights (bytes, normalized as OpenGL reads them)
		if( tr_ulInputs&IN_IDX) strGLSL += "  const vec4 v_idx = (K.ulStreams & 128u) != 0u ? GpuIdx(vid) : vec4(0.0f, 0.0f, 0.0f, 1.0f);\n";
		if( tr_ulInputs&IN_WGH) strGLSL += "  const vec4 v_wgh = (K.ulStreams & 128u) != 0u ? GpuWgh(vid) : vec4(0.0f, 0.0f, 0.0f, 1.0f);\n";
		if( tr_ulInputs&IN_TAN) strGLSL += "  const vec4 v_tan = (K.ulStreams & 256u) != 0u ? GpuTan(vid) : vec4(0.0f, 0.0f, 0.0f, 1.0f);\n";
	} else {
		strGLSL +=
			"void main()\n"
			"{\n"
			"  vec4 o_col = vec4(0.0f);\n";
	}
	if( tr_bAddress) strGLSL += "  int a0 = 0;\n";
	for( size_t i=0; i<tr_astrDecl.size(); i++) strGLSL += "  " + tr_astrDecl[i] + "\n";
	for( size_t i=0; i<tr_astrBody.size(); i++) strGLSL += "  " + tr_astrBody[i] + "\n";
	if( tr_bVertex) {
		strGLSL += "}\n";
	} else {
		// OpenGL's alpha test comes after the program
		strGLSL += "  if (K.bAlphaTest != 0u && o_col.a < K.fAlphaRef) discard;\n";
		strGLSL += "  gpu_FragColor = o_col;\n}\n";
	}
}

} // namespace


bool TranslateARBToMSL(const std::string &strARB, std::string &strMSL, std::string &strError)
{
	Translator tr(_dMSL);
	return tr.Translate(strARB, strMSL, strError);
}

bool TranslateARBToGLSL(const std::string &strARB, std::string &strGLSL, std::string &strError)
{
	Translator tr(_dGLSL);
	return tr.Translate(strARB, strGLSL, strError);
}
