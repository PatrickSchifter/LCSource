#include "StdH.h"

#include <Engine/Graphics/ShaderAsmToARB.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <set>
#include <vector>

// Line by line: each Direct3D instruction becomes one or a few ARB instructions on registers of
// the same names (r0.., t0.., c[n]), so a program reads like its source. What Direct3D does
// implicitly is written out:
//  - source modifiers of pixel shaders (1-x, _bias, _bx2, _x2) and instruction modifiers
//    (_x2, _d2, _sat...) as extra instructions on scratch temporaries (s0..);
//  - matrix macros (m4x4, m4x3, m3x3...) as one DP4/DP3 per row;
//  - D3DCOLOR inputs (v2, v3) with x and z swapped, as Direct3D reads the bytes;
//  - temporaries read before they are written start at zero, as the D3D8 shim on Windows does;
//  - ARB_vertex_program allows one vertex attribute and one program parameter per instruction,
//    so further ones are copied to scratch temporaries first.

namespace {

typedef std::string Str;

static const char *_strXYZW = "xyzw";

// a source or destination operand as written in Direct3D assembly (without whitespace)
struct Operand {
	bool bNeg;          // -x
	bool bInvert;       // 1-x
	Str  strMod;        // source modifier after '_' (bx2, bias, x2, dw, dz...)
	char chFile;        // r v c t a o
	Str  strOut;        // output register name, lower case (pos, d0, t0, fog, pts)
	int  iIndex;
	bool bRelative;     // c[a0.x+n]
	Str  strSwizzle;    // as written, rgba turned into xyzw

	Operand() : bNeg(false), bInvert(false), chFile(0), iIndex(0), bRelative(false) {}
};

// a source operand in ARB terms
struct Source {
	Str  strBase;  // register name, with array index
	Str  strSwz;   // four components
	bool bNeg;
	char chKind;   // 't' temporary, 'a' vertex attribute, 'p' program parameter, 'f' fragment input

	Str Text(void) const {
		Str str = bNeg ? "-" : "";
		str += strBase;
		if( strSwz=="xyzw") return str;
		if( strSwz[0]==strSwz[1] && strSwz[1]==strSwz[2] && strSwz[2]==strSwz[3]) return str + "." + strSwz[0];
		return str + "." + strSwz;
	}
};

static Str Lower(const Str &str)
{
	Str strRet = str;
	for( size_t i=0; i<strRet.size(); i++) strRet[i] = (char)tolower((unsigned char)strRet[i]);
	return strRet;
}

// "" means all four; one to three components are padded with the last one
static Str Swizzle4(const Str &strSwz)
{
	if( strSwz.empty()) return "xyzw";
	Str str = strSwz.substr(0, 4);
	while( str.size()<4) str += str[str.size()-1];
	return str;
}

// destination mask in xyzw order ("" means all four)
static Str Mask4(const Str &strMask)
{
	if( strMask.empty()) return "xyzw";
	Str str;
	for( int i=0; i<4; i++) {
		if( strMask.find(_strXYZW[i])!=Str::npos) str += _strXYZW[i];
	}
	return str;
}

static bool ParseOperand(const Str &str, Operand &op, Str &strError)
{
	op = Operand();
	size_t i = 0;
	if( str.compare(i, 1, "-")==0)  { op.bNeg = true;    i += 1; }
	if( str.compare(i, 2, "1-")==0) { op.bInvert = true; i += 2; }
	if( i>=str.size()) { strError = "empty operand"; return false; }

	const char ch = (char)tolower((unsigned char)str[i]);
	if( ch=='o') {
		size_t j = i+1;
		while( j<str.size() && isalnum((unsigned char)str[j])) j++;
		op.chFile = 'o';
		op.strOut = Lower(str.substr(i+1, j-i-1));
		i = j;
	} else if( ch=='c' && i+1<str.size() && str[i+1]=='[') {
		const size_t j = str.find(']', i);
		if( j==Str::npos) { strError = "unclosed [ in " + str; return false; }
		Str strInner = Lower(str.substr(i+2, j-i-2));
		op.chFile = 'c';
		const size_t iA0 = strInner.find("a0.x");
		if( iA0!=Str::npos) {
			op.bRelative = true;
			strInner.erase(iA0, 4);
			size_t iPlus;
			while( (iPlus=strInner.find('+'))!=Str::npos) strInner.erase(iPlus, 1);
		}
		op.iIndex = atoi(strInner.c_str());
		i = j+1;
	} else if( strchr("rvcta", ch)!=NULL) {
		size_t j = i+1;
		while( j<str.size() && isdigit((unsigned char)str[j])) j++;
		if( j==i+1) { strError = "register without number: " + str; return false; }
		op.chFile = ch;
		op.iIndex = atoi(str.substr(i+1, j-i-1).c_str());
		i = j;
	} else {
		strError = "unknown register " + str;
		return false;
	}

	// modifier and swizzle, in either order
	while( i<str.size()) {
		size_t j = i+1;
		if( str[i]=='_') {
			while( j<str.size() && isalnum((unsigned char)str[j])) j++;
			op.strMod = Lower(str.substr(i+1, j-i-1));
		} else if( str[i]=='.') {
			while( j<str.size() && isalpha((unsigned char)str[j])) j++;
			const Str strSwz = Lower(str.substr(i+1, j-i-1));
			op.strSwizzle = "";
			for( size_t k=0; k<strSwz.size(); k++) {
				const char *pch = strchr("rgba", strSwz[k]);
				const char chC = (pch!=NULL) ? _strXYZW[pch-"rgba"] : strSwz[k];
				if( strchr(_strXYZW, chC)==NULL) { strError = "bad swizzle in " + str; return false; }
				op.strSwizzle += chC;
			}
		} else {
			strError = "cannot parse operand " + str;
			return false;
		}
		i = j;
	}
	return true;
}


class Translator {
public:
	Translator(bool bVertex) : tr_bVertex(bVertex), tr_iVersion(0), tr_bAddress(false), tr_ctScratch(0), tr_ctMaxScratch(0) {}
	bool Translate(const char *strD3D, Str &strARB, Str &strError);

private:
	bool tr_bVertex;
	int  tr_iVersion;   // 11 to 14
	bool tr_bAddress;
	int  tr_ctScratch;  // scratch temporaries in use by the current instruction
	int  tr_ctMaxScratch;
	Str  tr_strError;
	std::vector<Str> tr_astrBody;
	std::vector<Str> tr_astrTemps;            // in order of first use
	std::map<Str,int> tr_mapWritten;          // temporary -> components written so far (bits)
	std::set<Str> tr_setInit;                 // temporaries read before written
	std::map<Str,Str> tr_mapConstants;        // literal -> PARAM name
	std::vector<Str> tr_astrParams;           // PARAM declarations
	std::map<int,Str> tr_mapDefs;             // constant register -> name of its def

	// previous instruction, for co-issue hazards
	Str tr_strLastDest;
	int tr_iLastMask;

	bool Fail(const Str &str) { if( tr_strError.empty()) tr_strError = str; return false; }
	void Emit(const Str &str) { tr_astrBody.push_back(str + ";"); }
	void UseTemp(const Str &strName) {
		for( size_t i=0; i<tr_astrTemps.size(); i++) if( tr_astrTemps[i]==strName) return;
		tr_astrTemps.push_back(strName);
	}
	Str Scratch(void) {
		char str[8];
		sprintf(str, "s%d", tr_ctScratch++);
		if( tr_ctScratch>tr_ctMaxScratch) tr_ctMaxScratch = tr_ctScratch;
		UseTemp(str);
		tr_mapWritten[str] = 15;
		return str;
	}
	Str Constant(float f0, float f1, float f2, float f3);

	void MarkRead(const Str &strTemp, const Str &strSwz4);
	void MarkWritten(const Str &strTemp, const Str &strMask4);

	bool GetSource(const Operand &op, Source &src);
	bool GetDest(const Operand &op, Str &strBase, Str &strMask4);
	void FixVertexOperands(std::vector<Source> &asrc);
	Str DestText(const Str &strBase, const Str &strMask4) const {
		return (strMask4=="xyzw") ? strBase : strBase + "." + strMask4;
	}

	bool VertexInstruction(const Str &strOp, std::vector<Operand> &aop);
	bool PixelInstruction(const Str &strOp, std::vector<Operand> &aop, const std::vector<Str> &astrRaw, bool bCoIssue);
};


Str Translator::Constant(float f0, float f1, float f2, float f3)
{
	char strValue[128];
	sprintf(strValue, "{ %.9g, %.9g, %.9g, %.9g }", f0, f1, f2, f3);
	std::map<Str,Str>::iterator it = tr_mapConstants.find(strValue);
	if( it!=tr_mapConstants.end()) return it->second;
	char strName[16];
	sprintf(strName, "k%d", (int)tr_mapConstants.size());
	tr_mapConstants[strValue] = strName;
	tr_astrParams.push_back(Str("PARAM ") + strName + " = " + strValue + ";");
	return strName;
}


void Translator::MarkRead(const Str &strTemp, const Str &strSwz4)
{
	const int iWritten = tr_mapWritten[strTemp];
	for( size_t i=0; i<strSwz4.size(); i++) {
		const int iBit = 1 << (strchr(_strXYZW, strSwz4[i]) - _strXYZW);
		if( !(iWritten&iBit)) { tr_setInit.insert(strTemp); return; }
	}
}

void Translator::MarkWritten(const Str &strTemp, const Str &strMask4)
{
	int &iWritten = tr_mapWritten[strTemp];
	for( size_t i=0; i<strMask4.size(); i++) iWritten |= 1 << (strchr(_strXYZW, strMask4[i]) - _strXYZW);
}


// source operand in ARB terms, with pixel shader source modifiers already applied
bool Translator::GetSource(const Operand &op, Source &src)
{
	char str[64];
	src.bNeg = op.bNeg;
	src.strSwz = Swizzle4(op.strSwizzle);
	bool bTemp = false;

	if( tr_bVertex) {
		switch( op.chFile) {
		case 'r':
			if( op.iIndex>=12) return Fail("no such temporary register");
			sprintf(str, "r%d", op.iIndex);  src.strBase = str;  src.chKind = 't';  bTemp = true;
			break;
		case 'v':
			src.chKind = 'a';
			switch( op.iIndex) {
			case 0: src.strBase = "vertex.position";  break;
			case 1: src.strBase = "vertex.normal";    break;
			case 2: sprintf(str, "vertex.attrib[%d]", ARB_ATTRIB_WEIGHTS);  src.strBase = str;  break;
			case 3: sprintf(str, "vertex.attrib[%d]", ARB_ATTRIB_INDICES);  src.strBase = str;  break;
			case 4: src.strBase = "vertex.color";     break;
			case 5: case 6: case 7: case 8:
				sprintf(str, "vertex.texcoord[%d]", op.iIndex-5);  src.strBase = str;  break;
			case 9: sprintf(str, "vertex.attrib[%d]", ARB_ATTRIB_TANGENT);  src.strBase = str;  break;
			default: return Fail("no such vertex input");
			}
			// D3DCOLOR: Direct3D's x is the third byte, which OpenGL reads as z
			if( op.iIndex==2 || op.iIndex==3) {
				for( int i=0; i<4; i++) {
					if( src.strSwz[i]=='x') src.strSwz[i] = 'z';
					else if( src.strSwz[i]=='z') src.strSwz[i] = 'x';
				}
			}
			break;
		case 'c':
			src.chKind = 'p';
			if( op.bRelative) {
				tr_bAddress = true;
				if( op.iIndex<-64 || op.iIndex>63) return Fail("relative offset out of range");
				if( op.iIndex>=0) sprintf(str, "c[a0.x+%d]", op.iIndex);
				else sprintf(str, "c[a0.x-%d]", -op.iIndex);
				src.strBase = str;
			} else if( tr_mapDefs.count(op.iIndex)) {
				src.strBase = tr_mapDefs[op.iIndex];
			} else {
				if( op.iIndex<0 || op.iIndex>=ARB_VERTEX_CONSTANTS) return Fail("no such constant register");
				sprintf(str, "c[%d]", op.iIndex);  src.strBase = str;
			}
			break;
		default:
			return Fail("register cannot be read in a vertex shader");
		}
		if( op.bInvert || !op.strMod.empty()) return Fail("source modifier in a vertex shader");
	} else {
		switch( op.chFile) {
		case 'r':
			if( op.iIndex>=6) return Fail("no such temporary register");
			sprintf(str, "r%d", op.iIndex);  src.strBase = str;  src.chKind = 't';  bTemp = true;
			break;
		case 't':
			if( op.iIndex>=8) return Fail("no such texture register");
			if( tr_iVersion<14) {
				sprintf(str, "t%d", op.iIndex);  src.chKind = 't';  bTemp = true;
			} else {
				sprintf(str, "fragment.texcoord[%d]", op.iIndex);  src.chKind = 'f';
			}
			src.strBase = str;
			break;
		case 'v':
			if( op.iIndex>=2) return Fail("no such color register");
			src.strBase = op.iIndex==0 ? "fragment.color.primary" : "fragment.color.secondary";
			src.chKind = 'f';
			break;
		case 'c':
			if( op.bRelative || op.iIndex<0 || op.iIndex>=8) return Fail("no such constant register");
			if( tr_mapDefs.count(op.iIndex)) {
				src.strBase = tr_mapDefs[op.iIndex];
			} else {
				sprintf(str, "program.env[%d]", op.iIndex);  src.strBase = str;
			}
			src.chKind = 'p';
			break;
		default:
			return Fail("register cannot be read in a pixel shader");
		}
	}
	if( bTemp) MarkRead(src.strBase, src.strSwz);

	// source modifiers (pixel shaders): computed on all four components into a scratch register
	Str strFormula;
	const Str strSrc = src.strBase;
	if( op.bInvert) {
		if( !op.strMod.empty()) return Fail("1-x with another modifier");
		strFormula = "SUB %s, " + Constant(1,1,1,1) + ", " + strSrc;
	} else if( op.strMod=="bias") {
		strFormula = "SUB %s, " + strSrc + ", " + Constant(0.5f,0.5f,0.5f,0.5f);
	} else if( op.strMod=="bx2") {
		strFormula = "MAD %s, " + strSrc + ", " + Constant(2,2,2,2) + ", -" + Constant(1,1,1,1);
	} else if( op.strMod=="x2") {
		strFormula = "ADD %s, " + strSrc + ", " + strSrc;
	} else if( !op.strMod.empty()) {
		return Fail("source modifier _" + op.strMod);
	}
	if( !strFormula.empty()) {
		const Str strScratch = Scratch();
		char strLine[256];
		sprintf(strLine, strFormula.c_str(), strScratch.c_str());
		Emit(strLine);
		src.strBase = strScratch;
		src.chKind = 't';
	}
	return true;
}


bool Translator::GetDest(const Operand &op, Str &strBase, Str &strMask4)
{
	char str[64];
	if( op.bNeg || op.bInvert || !op.strMod.empty() || op.bRelative) return Fail("modifier on a destination");
	strMask4 = Mask4(op.strSwizzle);
	if( strMask4.empty()) return Fail("empty write mask");

	switch( op.chFile) {
	case 'r':
		if( op.iIndex>=(tr_bVertex ? 12 : 6)) return Fail("no such temporary register");
		sprintf(str, "r%d", op.iIndex);  strBase = str;
		break;
	case 't':
		if( tr_bVertex || tr_iVersion>=14 || op.iIndex>=4) return Fail("texture register cannot be written");
		sprintf(str, "t%d", op.iIndex);  strBase = str;
		break;
	case 'o':
		if( !tr_bVertex) return Fail("output register in a pixel shader");
		if( op.strOut=="pos")      strBase = "result.position";
		else if( op.strOut=="d0")  strBase = "result.color";
		else if( op.strOut=="d1")  strBase = "result.color.secondary";
		else if( op.strOut=="fog") strBase = "result.fogcoord";
		else if( op.strOut=="pts") strBase = "result.pointsize";
		else if( op.strOut.size()==2 && op.strOut[0]=='t' && isdigit((unsigned char)op.strOut[1])) {
			sprintf(str, "result.texcoord[%c]", op.strOut[1]);  strBase = str;
		} else return Fail("no such output register o" + op.strOut);
		return true;
	default:
		return Fail("register cannot be written");
	}
	UseTemp(strBase);
	return true;
}


// one vertex attribute and one program parameter per ARB vertex program instruction
void Translator::FixVertexOperands(std::vector<Source> &asrc)
{
	Str strAttrib, strParam;
	for( size_t i=0; i<asrc.size(); i++) {
		Source &src = asrc[i];
		Str *pstrFirst = (src.chKind=='a') ? &strAttrib : (src.chKind=='p') ? &strParam : NULL;
		if( pstrFirst==NULL) continue;
		if( pstrFirst->empty()) { *pstrFirst = src.strBase; continue; }
		if( *pstrFirst==src.strBase) continue;
		// another one: copy it, and its other uses in this instruction, to a scratch register
		const Str strOld = src.strBase;
		const Str strScratch = Scratch();
		Emit("MOV " + strScratch + ", " + strOld);
		for( size_t j=i; j<asrc.size(); j++) {
			if( asrc[j].strBase==strOld) { asrc[j].strBase = strScratch;  asrc[j].chKind = 't'; }
		}
	}
}


bool Translator::VertexInstruction(const Str &strOp, std::vector<Operand> &aop)
{
	// simple instructions
	struct OpInfo { const char *strD3D; const char *strARB; int ctSources; bool bScalar; };
	static const OpInfo aoi[] = {
		{"mov","MOV",1,false}, {"add","ADD",2,false}, {"sub","SUB",2,false}, {"mul","MUL",2,false},
		{"mad","MAD",3,false}, {"dp3","DP3",2,false}, {"dp4","DP4",2,false}, {"min","MIN",2,false},
		{"max","MAX",2,false}, {"slt","SLT",2,false}, {"sge","SGE",2,false}, {"frc","FRC",1,false},
		{"lit","LIT",1,false}, {"dst","DST",2,false}, {"rcp","RCP",1,true},  {"rsq","RSQ",1,true},
		{"expp","EXP",1,true}, {"logp","LOG",1,true}, {"exp","EX2",1,true},  {"log","LG2",1,true},
	};

	if( strOp=="nop") return true;

	// matrix macros: one dot product per row
	if( strOp.size()==4 && strOp[0]=='m' && strOp[2]=='x') {
		const int ctColumns = strOp[1]-'0';
		const int ctRows    = strOp[3]-'0';
		if( (ctColumns!=3 && ctColumns!=4) || ctRows<2 || ctRows>4) return Fail("unknown instruction " + strOp);
		if( aop.size()!=3) return Fail("wrong operand count for " + strOp);
		// matrix rows are consecutive constants or temporaries
		if( aop[2].chFile!='c' && aop[2].chFile!='r') return Fail(strOp + " needs a matrix in c or r registers");
		Str strDest, strMask;
		if( !GetDest(aop[0], strDest, strMask)) return false;
		Source srcVector;
		if( !GetSource(aop[1], srcVector)) return false;
		// writing the vector or a matrix row being read: go thru a scratch register
		bool bAlias = (srcVector.chKind=='t' && srcVector.strBase==strDest);
		if( aop[0].chFile=='r' && aop[2].chFile=='r') {
			bAlias |= (aop[0].iIndex>=aop[2].iIndex && aop[0].iIndex<aop[2].iIndex+ctRows);
		}
		const Str strTarget = bAlias ? Scratch() : strDest;
		Str strWritten;
		for( int iRow=0; iRow<ctRows; iRow++) {
			const char chComp = _strXYZW[iRow];
			if( strMask.find(chComp)==Str::npos) continue;
			Operand opRow = aop[2];
			opRow.iIndex += iRow;
			opRow.strSwizzle = "";
			std::vector<Source> asrc(2);
			asrc[0] = srcVector;
			if( !GetSource(opRow, asrc[1])) return false;
			FixVertexOperands(asrc);
			Emit(Str(ctColumns==4 ? "DP4 " : "DP3 ") + strTarget + "." + chComp + ", " + asrc[0].Text() + ", " + asrc[1].Text());
			strWritten += chComp;
		}
		if( bAlias && !strWritten.empty()) Emit("MOV " + DestText(strDest, strWritten) + ", " + strTarget);
		if( aop[0].chFile=='r') MarkWritten(strDest, strWritten);
		return true;
	}

	for( size_t iOp=0; iOp<sizeof(aoi)/sizeof(aoi[0]); iOp++) {
		const OpInfo &oi = aoi[iOp];
		if( strOp!=oi.strD3D) continue;
		if( (int)aop.size()!=oi.ctSources+1) return Fail("wrong operand count for " + strOp);

		std::vector<Source> asrc(oi.ctSources);
		for( int iSrc=0; iSrc<oi.ctSources; iSrc++) {
			if( !GetSource(aop[iSrc+1], asrc[iSrc])) return false;
		}
		// address register load
		if( aop[0].chFile=='a') {
			if( strOp!="mov" || aop[0].iIndex!=0) return Fail("address register written by " + strOp);
			tr_bAddress = true;
			asrc[0].strSwz = Str(4, asrc[0].strSwz[0]);
			Emit("ARL a0.x, " + asrc[0].Text());
			return true;
		}
		Str strDest, strMask;
		if( !GetDest(aop[0], strDest, strMask)) return false;
		// scalar instructions take the component in the w position, as Direct3D does
		if( oi.bScalar) asrc[0].strSwz = Str(4, asrc[0].strSwz[3]);
		FixVertexOperands(asrc);
		Str strLine = Str(oi.strARB) + " " + DestText(strDest, strMask);
		for( size_t iSrc=0; iSrc<asrc.size(); iSrc++) strLine += ", " + asrc[iSrc].Text();
		Emit(strLine);
		if( aop[0].chFile=='r') MarkWritten(strDest, strMask);
		return true;
	}
	return Fail("unknown instruction " + strOp);
}


bool Translator::PixelInstruction(const Str &strOpFull, std::vector<Operand> &aop, const std::vector<Str> &astrRaw, bool bCoIssue)
{
	char str[256];

	// instruction modifiers
	Str strOp;
	float fScale = 1.0f;
	bool bSat = false;
	{
		size_t iStart = 0;
		size_t iEnd = strOpFull.find('_');
		strOp = strOpFull.substr(0, iEnd);
		while( iEnd!=Str::npos) {
			iStart = iEnd+1;
			iEnd = strOpFull.find('_', iStart);
			const Str strMod = strOpFull.substr(iStart, iEnd==Str::npos ? Str::npos : iEnd-iStart);
			if( strMod=="sat") bSat = true;
			else if( strMod=="x2") fScale *= 2.0f;
			else if( strMod=="x4") fScale *= 4.0f;
			else if( strMod=="x8") fScale *= 8.0f;
			else if( strMod=="d2") fScale *= 0.5f;
			else if( strMod=="d4") fScale *= 0.25f;
			else if( strMod=="d8") fScale *= 0.125f;
			else return Fail("unknown instruction modifier _" + strMod);
		}
	}

	if( strOp=="nop" || strOp=="phase") return true;

	if( strOp=="def") {
		if( astrRaw.size()!=5 || aop.empty() || aop[0].chFile!='c') return Fail("bad def");
		sprintf(str, "cdef%d", aop[0].iIndex);
		tr_astrParams.push_back(Str("PARAM ") + str + " = { " + astrRaw[1] + ", " + astrRaw[2] + ", " + astrRaw[3] + ", " + astrRaw[4] + " };");
		tr_mapDefs[aop[0].iIndex] = str;
		return true;
	}

	// texture instructions
	if( strOp=="tex" || strOp=="texcoord") {
		if( tr_iVersion>=14 || aop.size()!=1 || aop[0].chFile!='t') return Fail("bad " + strOp);
		Str strDest, strMask;
		if( !GetDest(aop[0], strDest, strMask)) return false;
		if( strOp=="tex") {
			sprintf(str, "TEX %s, fragment.texcoord[%d], texture[%d], 2D", strDest.c_str(), aop[0].iIndex, aop[0].iIndex);
			Emit(str);
		} else {
			sprintf(str, "MOV_SAT %s, fragment.texcoord[%d]", strDest.c_str(), aop[0].iIndex);
			Emit(str);
			Emit("MOV " + strDest + ".w, " + Constant(1,1,1,1));
		}
		MarkWritten(strDest, "xyzw");
		return true;
	}
	if( strOp=="texkill") {
		if( aop.size()!=1) return Fail("bad texkill");
		Source src;
		if( aop[0].chFile=='t' && tr_iVersion<14) {
			sprintf(str, "fragment.texcoord[%d]", aop[0].iIndex);
			src.strBase = str;  src.strSwz = "xyzw";  src.bNeg = false;  src.chKind = 'f';
		} else if( !GetSource(aop[0], src)) return false;
		src.strSwz = "xyzz";  // the w component does not count
		Emit("KIL " + src.Text());
		return true;
	}
	if( strOp=="texld" || strOp=="texcrd") {
		if( tr_iVersion<14 || aop.size()!=2 || aop[0].chFile!='r') return Fail("bad " + strOp);
		Str strDest, strMask;
		if( !GetDest(aop[0], strDest, strMask)) return false;
		// coordinates: texcoord or (in phase 2) a temporary, maybe divided by z or w
		Operand opCoord = aop[1];
		const Str strDivide = opCoord.strMod;
		opCoord.strMod = "";
		if( !strDivide.empty() && strDivide!="dw" && strDivide!="dz") return Fail("bad modifier on " + strOp + " source");
		Source src;
		if( !GetSource(opCoord, src)) return false;
		Str strCoord = src.Text();
		if( !strDivide.empty()) {
			const Str strScratch = Scratch();
			const Str strRcp = Scratch();
			Emit("MOV " + strScratch + ", " + strCoord);
			Emit("RCP " + strRcp + ".x, " + strScratch + (strDivide=="dw" ? ".w" : ".z"));
			Emit("MUL " + strScratch + ", " + strScratch + ", " + strRcp + ".x");
			strCoord = strScratch;
		}
		if( strOp=="texld") {
			sprintf(str, "TEX %s, %s, texture[%d], 2D", DestText(strDest, strMask).c_str(), strCoord.c_str(), aop[0].iIndex);
			Emit(str);
		} else {
			Emit("MOV " + DestText(strDest, strMask) + ", " + strCoord);
		}
		MarkWritten(strDest, strMask);
		return true;
	}

	// arithmetic
	struct OpInfo { const char *strD3D; const char *strARB; int ctSources; };
	static const OpInfo aoi[] = {
		{"mov","MOV",1}, {"add","ADD",2}, {"sub","SUB",2}, {"mul","MUL",2}, {"mad","MAD",3},
		{"lrp","LRP",3}, {"dp3","DP3",2}, {"dp4","DP4",2}, {"cnd","CMP",3}, {"cmp","CMP",3},
	};
	for( size_t iOp=0; iOp<sizeof(aoi)/sizeof(aoi[0]); iOp++) {
		const OpInfo &oi = aoi[iOp];
		if( strOp!=oi.strD3D) continue;
		if( (int)aop.size()!=oi.ctSources+1) return Fail("wrong operand count for " + strOp);

		Str strDest, strMask;
		if( !GetDest(aop[0], strDest, strMask)) return false;
		std::vector<Source> asrc(oi.ctSources);
		for( int iSrc=0; iSrc<oi.ctSources; iSrc++) {
			// co-issued instruction reading what its pair just wrote: Direct3D reads the old value
			if( bCoIssue && aop[iSrc+1].chFile==aop[0].chFile && !tr_strLastDest.empty()) {
				Source srcCheck;
				srcCheck.strSwz = Swizzle4(aop[iSrc+1].strSwizzle);
				for( int i=0; i<4; i++) {
					const int iBit = 1 << (strchr(_strXYZW, srcCheck.strSwz[i]) - _strXYZW);
					sprintf(str, "%c%d", aop[iSrc+1].chFile, aop[iSrc+1].iIndex);
					if( tr_strLastDest==str && (tr_iLastMask&iBit)) return Fail("co-issued instruction reads its pair's result");
				}
			}
			if( !GetSource(aop[iSrc+1], asrc[iSrc])) return false;
		}

		if( strOp=="cnd") {
			// d = s0>0.5 ? s1 : s2  ->  CMP d, 0.5-s0, s1, s2
			const Str strScratch = Scratch();
			Emit("SUB " + strScratch + ", " + Constant(0.5f,0.5f,0.5f,0.5f) + ", " + asrc[0].Text());
			asrc[0].strBase = strScratch;  asrc[0].bNeg = false;  asrc[0].chKind = 't';
		} else if( strOp=="cmp") {
			// d = s0>=0 ? s1 : s2  ->  CMP d, s0, s2, s1
			std::swap(asrc[1], asrc[2]);
		}

		Str strArgs;
		for( size_t iSrc=0; iSrc<asrc.size(); iSrc++) strArgs += ", " + asrc[iSrc].Text();
		if( fScale==1.0f) {
			Emit(Str(oi.strARB) + (bSat ? "_SAT " : " ") + DestText(strDest, strMask) + strArgs);
		} else {
			const Str strScratch = Scratch();
			Emit(Str(oi.strARB) + " " + strScratch + strArgs);
			Emit(Str(bSat ? "MUL_SAT " : "MUL ") + DestText(strDest, strMask) + ", " + strScratch + ", " + Constant(fScale,fScale,fScale,fScale));
		}
		MarkWritten(strDest, strMask);
		tr_strLastDest = strDest;
		tr_iLastMask = 0;
		for( size_t i=0; i<strMask.size(); i++) tr_iLastMask |= 1 << (strchr(_strXYZW, strMask[i]) - _strXYZW);
		return true;
	}
	return Fail("unknown instruction " + strOp);
}


bool Translator::Translate(const char *strD3D, Str &strARB, Str &strError)
{
	const Str strSource = strD3D;
	size_t iLineStart = 0;
	int iLine = 0;
	while( iLineStart<strSource.size()) {
		size_t iLineEnd = strSource.find('\n', iLineStart);
		if( iLineEnd==Str::npos) iLineEnd = strSource.size();
		Str strLine = strSource.substr(iLineStart, iLineEnd-iLineStart);
		iLineStart = iLineEnd+1;
		iLine++;

		// comments
		size_t iComment = strLine.find("//");
		if( iComment!=Str::npos) strLine.erase(iComment);
		iComment = strLine.find(';');
		if( iComment!=Str::npos) strLine.erase(iComment);
		// opcode
		size_t i = 0;
		while( i<strLine.size() && isspace((unsigned char)strLine[i])) i++;
		if( i==strLine.size()) continue;
		bool bCoIssue = false;
		if( strLine[i]=='+') {
			bCoIssue = true;
			i++;
			while( i<strLine.size() && isspace((unsigned char)strLine[i])) i++;
		}
		size_t j = i;
		while( j<strLine.size() && !isspace((unsigned char)strLine[j])) j++;
		const Str strOp = Lower(strLine.substr(i, j-i));
		// operands, without whitespace
		std::vector<Str> astrRaw;
		Str strOperand;
		for( size_t k=j; k<=strLine.size(); k++) {
			if( k==strLine.size() || strLine[k]==',') {
				if( !strOperand.empty() || k<strLine.size()) astrRaw.push_back(strOperand);
				strOperand = "";
			} else if( !isspace((unsigned char)strLine[k])) {
				strOperand += strLine[k];
			}
		}

		char strWhere[32];
		sprintf(strWhere, "line %d: ", iLine);

		// version
		if( tr_iVersion==0) {
			const bool bVS = (strOp.compare(0, 3, "vs.")==0);
			const bool bPS = (strOp.compare(0, 3, "ps.")==0);
			if( (!bVS && !bPS) || bVS!=tr_bVertex) { strError = Str(strWhere) + "expected version, found " + strOp; return false; }
			tr_iVersion = (strOp[3]-'0')*10 + (strOp.size()>5 ? strOp[5]-'0' : 0);
			if( tr_iVersion<10 || tr_iVersion>(tr_bVertex ? 11 : 14)) { strError = Str(strWhere) + "unsupported version " + strOp; return false; }
			continue;
		}

		std::vector<Operand> aop(astrRaw.size());
		for( size_t iOp=0; iOp<astrRaw.size(); iOp++) {
			// def's numbers are not registers
			if( strOp=="def" && iOp>0) continue;
			Str strErr;
			if( !ParseOperand(astrRaw[iOp], aop[iOp], strErr)) { strError = Str(strWhere) + strErr; return false; }
		}

		tr_ctScratch = 0;
		const bool bOK = tr_bVertex ? VertexInstruction(strOp, aop) : PixelInstruction(strOp, aop, astrRaw, bCoIssue);
		if( !bOK) { strError = Str(strWhere) + tr_strError; return false; }
	}
	if( tr_iVersion==0) { strError = "no version"; return false; }

	// put it together
	strARB = tr_bVertex ? "!!ARBvp1.0\n" : "!!ARBfp1.0\n";
	if( tr_bVertex) {
		char str[64];
		sprintf(str, "PARAM c[%d] = { program.env[0..%d] };\n", ARB_VERTEX_CONSTANTS, ARB_VERTEX_CONSTANTS-1);
		strARB += str;
		if( tr_bAddress) strARB += "ADDRESS a0;\n";
	}
	// registers read before written start at zero
	if( !tr_bVertex) tr_setInit.insert("r0");  // the output (a shader that writes only part of it)
	std::vector<Str> astrInit;
	for( std::set<Str>::iterator it=tr_setInit.begin(); it!=tr_setInit.end(); ++it) {
		UseTemp(*it);
		astrInit.push_back("MOV " + *it + ", " + Constant(0,0,0,0) + ";");
	}
	for( size_t i=0; i<tr_astrParams.size(); i++) strARB += tr_astrParams[i] + "\n";
	if( !tr_astrTemps.empty()) {
		strARB += "TEMP ";
		for( size_t i=0; i<tr_astrTemps.size(); i++) strARB += (i ? ", " : "") + tr_astrTemps[i];
		strARB += ";\n";
	}
	for( size_t i=0; i<astrInit.size(); i++) strARB += astrInit[i] + "\n";
	for( size_t i=0; i<tr_astrBody.size(); i++) strARB += tr_astrBody[i] + "\n";
	if( !tr_bVertex) strARB += "MOV_SAT result.color, r0;\n";
	strARB += "END\n";
	return true;
}

} // namespace


bool TranslateVertexProgramToARB(const char *strD3D, std::string &strARB, std::string &strError)
{
	Translator tr(true);
	return tr.Translate(strD3D, strARB, strError);
}

bool TranslatePixelProgramToARB(const char *strD3D, std::string &strARB, std::string &strError)
{
	Translator tr(false);
	return tr.Translate(strD3D, strARB, strError);
}
