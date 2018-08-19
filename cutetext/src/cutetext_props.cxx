// This file is part of CuteText project
// Copyright (C) 2018 by James Zeng <james.zeng@hotmail.com>
// The LICENSE file describes the conditions under which this software may be distributed.
/**
 * @file cutetext_props.cxx
 * @author James Zeng
 * @date 2018-08-12
 * @brief CuteText Properties management.
 *
 * @see https://github.com/cutetext/cutetext
 */

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <clocale>

#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <memory>

#include <fcntl.h>

#include "ILexer.h"
#include "Scintilla.h"
#include "SciLexer.h"

#include "GUI.h"
#include "ScintillaWindow.h"

#if defined(__unix__)

const GUI::gui_char menuAccessIndicator[] = GUI_TEXT("_");

#else

const GUI::gui_char menuAccessIndicator[] = GUI_TEXT("&");

#endif

#include "StringList.h"
#include "StringHelpers.h"
#include "FilePath.h"
#include "StyleDefinition.h"
#include "PropSetFile.h"
#include "StyleWriter.h"
#include "Extender.h"
#include "SciTE.h"
#include "Mutex.h"
#include "JobQueue.h"
#include "Cookie.h"
#include "Worker.h"
#include "MatchMarker.h"
#include "EditorConfig.h"
#include "SciTEBase.h"
#include "IFaceTable.h"

void SciTEBase::SetImportMenu() {
	for (int i = 0; i < kImportMax; i++) {
		DestroyMenuItem(kMenuOptions, kImportCmdID + i);
	}
	if (!importFiles_.empty()) {
		for (int stackPos = 0; stackPos < static_cast<int>(importFiles_.size()) && stackPos < kImportMax; stackPos++) {
			const int itemID = kImportCmdID + stackPos;
			if (importFiles_[stackPos].IsSet()) {
				GUI::gui_string entry = localiser.Text("Open");
				entry += GUI_TEXT(" ");
				entry += importFiles_[stackPos].Name().AsInternal();
				SetMenuItem(kMenuOptions, IMPORT_START + stackPos, itemID, entry.c_str());
			}
		}
	}
}

void SciTEBase::ImportMenu(int pos) {
	if (pos >= 0) {
		if (importFiles_[pos].IsSet()) {
			Open(importFiles_[pos]);
		}
	}
}

void SciTEBase::SetLanguageMenu() {
	for (int i = 0; i < 100; i++) {
		DestroyMenuItem(kMenuLanguage, kLanguageCmdID + i);
	}
	for (unsigned int item = 0; item < languageMenu_.size(); item++) {
		const int itemID = kLanguageCmdID + item;
		GUI::gui_string entry = localiser.Text(languageMenu_[item].menuItem.c_str());
		if (languageMenu_[item].menuKey.length()) {
#if defined(GTK)
			entry += GUI_TEXT(" ");
#else
			entry += GUI_TEXT("\t");
#endif
			entry += GUI::StringFromUTF8(languageMenu_[item].menuKey);
		}
		if (entry.size() && entry[0] != '#') {
			SetMenuItem(kMenuLanguage, item, itemID, entry.c_str());
		}
	}
}

// Null except on Windows where it may be overridden
void SciTEBase::ReadEmbeddedProperties() {
}

const GUI::gui_char propLocalFileName[] = GUI_TEXT("SciTE.properties");
const GUI::gui_char propDirectoryFileName[] = GUI_TEXT("SciTEDirectory.properties");

void SciTEBase::ReadEnvironment() {
#if defined(__unix__)
	extern char **environ;
	char **e = environ;
#else
	char **e = _environ;
#endif
	for (; e && *e; e++) {
		char key[1024];
		char *k = *e;
		char *v = strchr(k, '=');
		if (v && (static_cast<size_t>(v - k) < sizeof(key))) {
			memcpy(key, k, v - k);
			key[v - k] = '\0';
			propsPlatform.Set(key, v + 1);
		}
	}
}

/**
Read global and user properties files.
*/
void SciTEBase::ReadGlobalPropFile() {
	std::string excludes;
	std::string includes;

	// Want to apply imports.exclude and imports.include but these may well be in
	// user properties.

	for (int attempt=0; attempt<2; attempt++) {

		std::string excludesRead = props.GetString("imports.exclude");
		std::string includesRead = props.GetString("imports.include");
		if ((attempt > 0) && ((excludesRead == excludes) && (includesRead == includes)))
			break;

		excludes = excludesRead;
		includes = includesRead;

		filter_.SetFilter(excludes.c_str(), includes.c_str());

		importFiles_.clear();

		ReadEmbeddedProperties();

		propsBase.Clear();
		FilePath propfileBase = GetDefaultPropertiesFileName();
		propsBase.Read(propfileBase, propfileBase.Directory(), filter_, &importFiles_, 0);

		propsUser.Clear();
		FilePath propfileUser = GetUserPropertiesFileName();
		propsUser.Read(propfileUser, propfileUser.Directory(), filter_, &importFiles_, 0);
	}

	if (!localiser.read) {
		ReadLocalization();
	}
}

void SciTEBase::ReadAbbrevPropFile() {
	propsAbbrev.Clear();
	propsAbbrev.Read(pathAbbreviations, pathAbbreviations.Directory(), filter_, &importFiles_, 0);
}

/**
Reads the directory properties file depending on the variable
"properties.directory.enable". Also sets the variable $(SciteDirectoryHome) to the path
where this property file is found. If it is not found $(SciteDirectoryHome) will
be set to $(FilePath).
*/
void SciTEBase::ReadDirectoryPropFile() {
	propsDirectory.Clear();

	if (props.GetInt("properties.directory.enable") != 0) {
		FilePath propfile = GetDirectoryPropertiesFileName();
		props.Set("SciteDirectoryHome", propfile.Directory().AsUTF8().c_str());

		propsDirectory.Read(propfile, propfile.Directory(), filter_, NULL, 0);
	}
}

/**
Read local and directory properties file.
*/
void SciTEBase::ReadLocalPropFile() {
	// The directory properties acts like a base local properties file.
	// Therefore it must be read always before reading the local prop file.
	ReadDirectoryPropFile();

	FilePath propfile = GetLocalPropertiesFileName();

	propsLocal.Clear();
	propsLocal.Read(propfile, propfile.Directory(), filter_, NULL, 0);

	props.Set("Chrome", "#C0C0C0");
	props.Set("ChromeHighlight", "#FFFFFF");

	FilePath fileDirectory = filePath_.Directory();
	editorConfig->Clear();
	if (props.GetInt("editor.config.enable", 0)) {
		editorConfig->ReadFromDirectory(fileDirectory);
	}
}

Colour ColourOfProperty(const PropSetFile &props, const char *key, Colour colourDefault) {
	std::string colour = props.GetExpandedString(key);
	if (colour.length()) {
		return ColourFromString(colour);
	}
	return colourDefault;
}

/**
 * Put the next property item from the given property string
 * into the buffer pointed by @a pPropItem.
 * @return NULL if the end of the list is met, else, it points to the next item.
 */
const char *SciTEBase::GetNextPropItem(
	const char *pStart,	/**< the property string to parse for the first call,
						 * pointer returned by the previous call for the following. */
	char *pPropItem,	///< pointer on a buffer receiving the requested prop item
	int maxLen)			///< size of the above buffer
{
	ptrdiff_t size = maxLen - 1;

	*pPropItem = '\0';
	if (pStart == NULL) {
		return NULL;
	}
	const char *pNext = strchr(pStart, ',');
	if (pNext) {	// Separator is found
		if (size > pNext - pStart) {
			// Found string fits in buffer
			size = pNext - pStart;
		}
		pNext++;
	}
	strncpy(pPropItem, pStart, size);
	pPropItem[size] = '\0';
	return pNext;
}

std::string SciTEBase::StyleString(const char *lang, int style) const {
	char key[200];
	sprintf(key, "style.%s.%0d", lang, style);
	return props.GetExpandedString(key);
}

StyleDefinition SciTEBase::StyleDefinitionFor(int style) {
	const std::string languageName = !StartsWith(language_, "lpeg_") ? language_ : "lpeg";

	const std::string ssDefault = StyleString("*", style);
	std::string ss = StyleString(languageName.c_str(), style);

	if (!subStyleBases_.empty()) {
		const int baseStyle = wEditor_.Call(SCI_GETSTYLEFROMSUBSTYLE, style);
		if (baseStyle != style) {
			const int primaryStyle = wEditor_.Call(SCI_GETPRIMARYSTYLEFROMSTYLE, style);
			const int distanceSecondary = (style == primaryStyle) ? 0 : wEditor_.Call(SCI_DISTANCETOSECONDARYSTYLES);
			const int primaryBase = baseStyle - distanceSecondary;
			const int subStylesStart = wEditor_.Call(SCI_GETSUBSTYLESSTART, primaryBase);
			const int subStylesLength = wEditor_.Call(SCI_GETSUBSTYLESLENGTH, primaryBase);
			const int subStyle = style - (subStylesStart + distanceSecondary);
			if (subStyle < subStylesLength) {
				char key[200];
				sprintf(key, "style.%s.%0d.%0d", languageName.c_str(), baseStyle, subStyle + 1);
				ss = props.GetNewExpandString(key);
			}
		}
	}

	StyleDefinition sd(ssDefault);
	sd.ParseStyleDefinition(ss);
	return sd;
}

void SciTEBase::SetOneStyle(GUI::ScintillaWindow &win, int style, const StyleDefinition &sd) {
	if (sd.specified & StyleDefinition::sdItalics)
		win.Call(SCI_STYLESETITALIC, style, sd.italics ? 1 : 0);
	if (sd.specified & StyleDefinition::sdWeight)
		win.Call(SCI_STYLESETWEIGHT, style, sd.weight);
	if (sd.specified & StyleDefinition::sdFont)
		win.CallString(SCI_STYLESETFONT, style,
			sd.font.c_str());
	if (sd.specified & StyleDefinition::sdFore)
		win.Call(SCI_STYLESETFORE, style, sd.ForeAsLong());
	if (sd.specified & StyleDefinition::sdBack)
		win.Call(SCI_STYLESETBACK, style, sd.BackAsLong());
	if (sd.specified & StyleDefinition::sdSize)
		win.Call(SCI_STYLESETSIZEFRACTIONAL, style, sd.FractionalSize());
	if (sd.specified & StyleDefinition::sdEOLFilled)
		win.Call(SCI_STYLESETEOLFILLED, style, sd.eolfilled ? 1 : 0);
	if (sd.specified & StyleDefinition::sdUnderlined)
		win.Call(SCI_STYLESETUNDERLINE, style, sd.underlined ? 1 : 0);
	if (sd.specified & StyleDefinition::sdCaseForce)
		win.Call(SCI_STYLESETCASE, style, sd.caseForce);
	if (sd.specified & StyleDefinition::sdVisible)
		win.Call(SCI_STYLESETVISIBLE, style, sd.visible ? 1 : 0);
	if (sd.specified & StyleDefinition::sdChangeable)
		win.Call(SCI_STYLESETCHANGEABLE, style, sd.changeable ? 1 : 0);
	win.Call(SCI_STYLESETCHARACTERSET, style, characterSet_);
}

void SciTEBase::SetStyleBlock(GUI::ScintillaWindow &win, const char *lang, int start, int last) {
	for (int style = start; style <= last; style++) {
		if (style != STYLE_DEFAULT) {
			char key[200];
			sprintf(key, "style.%s.%0d", lang, style-start);
			std::string sval = props.GetExpandedString(key);
			if (sval.length()) {
				SetOneStyle(win, style, StyleDefinition(sval));
			}
		}
	}
}

void SciTEBase::SetStyleFor(GUI::ScintillaWindow &win, const char *lang) {
	SetStyleBlock(win, lang, 0, STYLE_MAX);
}

void SciTEBase::SetOneIndicator(GUI::ScintillaWindow &win, int indicator, const IndicatorDefinition &ind) {
	win.Call(SCI_INDICSETSTYLE, indicator, ind.style);
	win.Call(SCI_INDICSETFORE, indicator, ind.colour);
	win.Call(SCI_INDICSETALPHA, indicator, ind.fillAlpha);
	win.Call(SCI_INDICSETOUTLINEALPHA, indicator, ind.outlineAlpha);
	win.Call(SCI_INDICSETUNDER, indicator, ind.under);
}

std::string SciTEBase::ExtensionFileName() const {
	if (CurrentBufferConst()->overrideExtension.length()) {
		return CurrentBufferConst()->overrideExtension;
	} else {
		FilePath name = FileNameExt();
		if (name.IsSet()) {
#if !defined(GTK)
			// Force extension to lower case
			std::string extension = name.Extension().AsUTF8();
			if (extension.empty()) {
				return name.AsUTF8();
			} else {
				LowerCaseAZ(extension);
				return name.BaseName().AsUTF8() + "." + extension;
			}
#else
			return name.AsUTF8();
#endif
		} else {
			return props.GetString("default.file.ext");
		}
	}
}

void SciTEBase::ForwardPropertyToEditor(const char *key) {
	if (props.Exists(key)) {
		std::string value = props.GetExpandedString(key);
		wEditor_.CallString(SCI_SETPROPERTY,
						 UptrFromString(key), value.c_str());
		wOutput_.CallString(SCI_SETPROPERTY,
						 UptrFromString(key), value.c_str());
	}
}

void SciTEBase::DefineMarker(int marker, int markerType, Colour fore, Colour back, Colour backSelected) {
	wEditor_.Call(SCI_MARKERDEFINE, marker, markerType);
	wEditor_.Call(SCI_MARKERSETFORE, marker, fore);
	wEditor_.Call(SCI_MARKERSETBACK, marker, back);
	wEditor_.Call(SCI_MARKERSETBACKSELECTED, marker, backSelected);
}

void SciTEBase::ReadAPI(const std::string &fileNameForExtension) {
	std::string sApiFileNames = props.GetNewExpandString("api.",
	                        fileNameForExtension.c_str());
	if (sApiFileNames.length() > 0) {
		std::vector<std::string> vApiFileNames = StringSplit(sApiFileNames, ';');
		std::vector<char> data;

		// Load files into data
		for (const std::string &vApiFileName : vApiFileNames) {
			std::string contents = FilePath(GUI::StringFromUTF8(vApiFileName)).Read();
			data.insert(data.end(), contents.begin(), contents.end());
		}

		// Initialise apis
		if (data.size() > 0) {
			apis_.Set(data);
		}
	}
}

std::string SciTEBase::FindLanguageProperty(const char *pattern, const char *defaultValue) {
	std::string key = pattern;
	Substitute(key, "*", language_.c_str());
	std::string ret = props.GetExpandedString(key.c_str());
	if (ret == "")
		ret = props.GetExpandedString(pattern);
	if (ret == "")
		ret = defaultValue;
	return ret;
}

/**
 * A list of all the properties that should be forwarded to Scintilla lexers.
 */
static const char *propertiesToForward[] = {
	"fold.lpeg.by.indentation",
	"lexer.lpeg.color.theme",
	"lexer.lpeg.home",
	"lexer.lpeg.script",
//++Autogenerated -- run ../scripts/RegenerateSource.py to regenerate
//**\(\t"\*",\n\)
	"asp.default.language",
	"fold",
	"fold.abl.comment.multiline",
	"fold.abl.syntax.based",
	"fold.asm.comment.explicit",
	"fold.asm.comment.multiline",
	"fold.asm.explicit.anywhere",
	"fold.asm.explicit.end",
	"fold.asm.explicit.start",
	"fold.asm.syntax.based",
	"fold.at.else",
	"fold.baan.inner.level",
	"fold.baan.keywords.based",
	"fold.baan.sections",
	"fold.baan.syntax.based",
	"fold.basic.comment.explicit",
	"fold.basic.explicit.anywhere",
	"fold.basic.explicit.end",
	"fold.basic.explicit.start",
	"fold.basic.syntax.based",
	"fold.coffeescript.comment",
	"fold.comment",
	"fold.comment.nimrod",
	"fold.comment.yaml",
	"fold.compact",
	"fold.cpp.comment.explicit",
	"fold.cpp.comment.multiline",
	"fold.cpp.explicit.anywhere",
	"fold.cpp.explicit.end",
	"fold.cpp.explicit.start",
	"fold.cpp.preprocessor.at.else",
	"fold.cpp.syntax.based",
	"fold.d.comment.explicit",
	"fold.d.comment.multiline",
	"fold.d.explicit.anywhere",
	"fold.d.explicit.end",
	"fold.d.explicit.start",
	"fold.d.syntax.based",
	"fold.directive",
	"fold.haskell.imports",
	"fold.html",
	"fold.html.preprocessor",
	"fold.hypertext.comment",
	"fold.hypertext.heredoc",
	"fold.perl.at.else",
	"fold.perl.comment.explicit",
	"fold.perl.package",
	"fold.perl.pod",
	"fold.preprocessor",
	"fold.quotes.nimrod",
	"fold.quotes.python",
	"fold.rust.comment.explicit",
	"fold.rust.comment.multiline",
	"fold.rust.explicit.anywhere",
	"fold.rust.explicit.end",
	"fold.rust.explicit.start",
	"fold.rust.syntax.based",
	"fold.sql.at.else",
	"fold.sql.only.begin",
	"fold.verilog.flags",
	"html.tags.case.sensitive",
	"lexer.asm.comment.delimiter",
	"lexer.baan.styling.within.preprocessor",
	"lexer.caml.magic",
	"lexer.cpp.allow.dollars",
	"lexer.cpp.backquoted.strings",
	"lexer.cpp.escape.sequence",
	"lexer.cpp.hashquoted.strings",
	"lexer.cpp.track.preprocessor",
	"lexer.cpp.triplequoted.strings",
	"lexer.cpp.update.preprocessor",
	"lexer.cpp.verbatim.strings.allow.escapes",
	"lexer.css.hss.language",
	"lexer.css.less.language",
	"lexer.css.scss.language",
	"lexer.d.fold.at.else",
	"lexer.edifact.highlight.un.all",
	"lexer.errorlist.escape.sequences",
	"lexer.errorlist.value.separate",
	"lexer.flagship.styling.within.preprocessor",
	"lexer.haskell.allow.hash",
	"lexer.haskell.allow.questionmark",
	"lexer.haskell.allow.quotes",
	"lexer.haskell.cpp",
	"lexer.haskell.import.safe",
	"lexer.html.django",
	"lexer.html.mako",
	"lexer.json.allow.comments",
	"lexer.json.escape.sequence",
	"lexer.metapost.comment.process",
	"lexer.metapost.interface.default",
	"lexer.pascal.smart.highlighting",
	"lexer.props.allow.initial.spaces",
	"lexer.python.keywords2.no.sub.identifiers",
	"lexer.python.literals.binary",
	"lexer.python.strings.b",
	"lexer.python.strings.f",
	"lexer.python.strings.over.newline",
	"lexer.python.strings.u",
	"lexer.python.unicode.identifiers",
	"lexer.rust.fold.at.else",
	"lexer.sql.allow.dotted.word",
	"lexer.sql.backticks.identifier",
	"lexer.sql.numbersign.comment",
	"lexer.tex.auto.if",
	"lexer.tex.comment.process",
	"lexer.tex.interface.default",
	"lexer.tex.use.keywords",
	"lexer.verilog.allupperkeywords",
	"lexer.verilog.fold.preprocessor.else",
	"lexer.verilog.portstyling",
	"lexer.verilog.track.preprocessor",
	"lexer.verilog.update.preprocessor",
	"lexer.xml.allow.scripts",
	"nsis.ignorecase",
	"nsis.uservars",
	"ps.level",
	"sql.backslash.escapes",
	"styling.within.preprocessor",
	"tab.timmy.whinge.level",

//--Autogenerated -- end of automatically generated section

	0,
};

/* XPM */
static const char *bookmarkBluegem[] = {
/* width height num_colors chars_per_pixel */
"    15    15      64            1",
/* colors */
"  c none",
". c #0c0630",
"# c #8c8a8c",
"a c #244a84",
"b c #545254",
"c c #cccecc",
"d c #949594",
"e c #346ab4",
"f c #242644",
"g c #3c3e3c",
"h c #6ca6fc",
"i c #143789",
"j c #204990",
"k c #5c8dec",
"l c #707070",
"m c #3c82dc",
"n c #345db4",
"o c #619df7",
"p c #acacac",
"q c #346ad4",
"r c #1c3264",
"s c #174091",
"t c #5482df",
"u c #4470c4",
"v c #2450a0",
"w c #14162c",
"x c #5c94f6",
"y c #b7b8b7",
"z c #646464",
"A c #3c68b8",
"B c #7cb8fc",
"C c #7c7a7c",
"D c #3462b9",
"E c #7c7eac",
"F c #44464c",
"G c #a4a4a4",
"H c #24224c",
"I c #282668",
"J c #5c5a8c",
"K c #7c8ebc",
"L c #dcd7e4",
"M c #141244",
"N c #1c2e5c",
"O c #24327c",
"P c #4472cc",
"Q c #6ca2fc",
"R c #74b2fc",
"S c #24367c",
"T c #b4b2c4",
"U c #403e58",
"V c #4c7fd6",
"W c #24428c",
"X c #747284",
"Y c #142e7c",
"Z c #64a2fc",
"0 c #3c72dc",
"1 c #bcbebc",
"2 c #6c6a6c",
"3 c #848284",
"4 c #2c5098",
"5 c #1c1a1c",
"6 c #243250",
"7 c #7cbefc",
"8 c #d4d2d4",
/* pixels */
"    yCbgbCy    ",
"   #zGGyGGz#   ",
"  #zXTLLLTXz#  ",
" p5UJEKKKEJU5p ",
" lfISa444aSIfl ",
" wIYij444jsYIw ",
" .OsvnAAAnvsO. ",
" MWvDuVVVPDvWM ",
" HsDPVkxxtPDsH ",
" UiAtxohZxtuiU ",
" pNnkQRBRhkDNp ",
" 1FrqoR7Bo0rF1 ",
" 8GC6aemea6CG8 ",
"  cG3l2z2l3Gc  ",
"    1GdddG1    "
};

std::string SciTEBase::GetFileNameProperty(const char *name) {
	std::string namePlusDot = name;
	namePlusDot.append(".");
	std::string valueForFileName = props.GetNewExpandString(namePlusDot.c_str(),
	        ExtensionFileName().c_str());
	if (valueForFileName.length() != 0) {
		return valueForFileName;
	} else {
		return props.GetString(name);
	}
}

void SciTEBase::ReadProperties() {
	if (extender)
		extender->Clear();

	const std::string fileNameForExtension = ExtensionFileName();

	std::string modulePath = props.GetNewExpandString("lexerpath.",
	    fileNameForExtension.c_str());
	if (modulePath.length())
	    wEditor_.CallString(SCI_LOADLEXERLIBRARY, 0, modulePath.c_str());
	language_ = props.GetNewExpandString("lexer.", fileNameForExtension.c_str());
	if (wEditor_.Call(SCI_GETDOCUMENTOPTIONS) & SC_DOCUMENTOPTION_STYLES_NONE) {
		language_ = "";
	}
	if (language_.length()) {
		if (StartsWith(language_, "script_")) {
			wEditor_.Call(SCI_SETLEXER, SCLEX_CONTAINER);
		} else if (StartsWith(language_, "lpeg_")) {
			modulePath = props.GetNewExpandString("lexerpath.*.lpeg");
			if (modulePath.length()) {
				wEditor_.CallString(SCI_LOADLEXERLIBRARY, 0, modulePath.c_str());
				wEditor_.CallString(SCI_SETLEXERLANGUAGE, 0, "lpeg");
				lexLPeg_ = wEditor_.Call(SCI_GETLEXER);
				const char *lexer = language_.c_str() + language_.find('_') + 1;
				wEditor_.CallReturnPointer(SCI_PRIVATELEXERCALL, SCI_SETLEXERLANGUAGE,
					SptrFromString(lexer));
			}
		} else {
			wEditor_.CallString(SCI_SETLEXERLANGUAGE, 0, language_.c_str());
		}
	} else {
		wEditor_.Call(SCI_SETLEXER, SCLEX_NULL);
	}

	props.Set("Language", language_.c_str());

	lexLanguage_ = wEditor_.Call(SCI_GETLEXER);

	wOutput_.Call(SCI_SETLEXER, SCLEX_ERRORLIST);

	const std::string kw0 = props.GetNewExpandString("keywords.", fileNameForExtension.c_str());
	wEditor_.CallString(SCI_SETKEYWORDS, 0, kw0.c_str());

	for (int wl = 1; wl <= KEYWORDSET_MAX; wl++) {
		std::string kwk = StdStringFromInteger(wl+1);
		kwk += '.';
		kwk.insert(0, "keywords");
		const std::string kw = props.GetNewExpandString(kwk.c_str(), fileNameForExtension.c_str());
		wEditor_.CallString(SCI_SETKEYWORDS, wl, kw.c_str());
	}

	subStyleBases_.clear();
	const int lenSSB = wEditor_.CallString(SCI_GETSUBSTYLEBASES, 0, NULL);
	if (lenSSB) {
		wEditor_.Call(SCI_FREESUBSTYLES);

		subStyleBases_.resize(lenSSB+1);
		wEditor_.CallString(SCI_GETSUBSTYLEBASES, 0, &subStyleBases_[0]);
		subStyleBases_.resize(lenSSB);	// Remove NUL

		for (int baseStyle=0;baseStyle<lenSSB;baseStyle++) {
			//substyles.cpp.11=2
			std::string ssSubStylesKey = "substyles.";
			ssSubStylesKey += language_;
			ssSubStylesKey += ".";
			ssSubStylesKey += StdStringFromInteger(subStyleBases_[baseStyle]);
			std::string ssNumber = props.GetNewExpandString(ssSubStylesKey.c_str());
			int subStyleIdentifiers = atoi(ssNumber.c_str());

			int subStyleIdentifiersStart = 0;
			if (subStyleIdentifiers) {
				subStyleIdentifiersStart = wEditor_.Call(SCI_ALLOCATESUBSTYLES, subStyleBases_[baseStyle], subStyleIdentifiers);
				if (subStyleIdentifiersStart < 0)
					subStyleIdentifiers = 0;
			}
			for (int subStyle=0; subStyle<subStyleIdentifiers; subStyle++) {
				// substylewords.11.1.$(file.patterns.cpp)=CharacterSet LexAccessor SString WordList
				std::string ssWordsKey = "substylewords.";
				ssWordsKey += StdStringFromInteger(subStyleBases_[baseStyle]);
				ssWordsKey += ".";
				ssWordsKey += StdStringFromInteger(subStyle + 1);
				ssWordsKey += ".";
				std::string ssWords = props.GetNewExpandString(ssWordsKey.c_str(), fileNameForExtension.c_str());
				wEditor_.CallString(SCI_SETIDENTIFIERS, subStyleIdentifiersStart + subStyle, ssWords.c_str());
			}
		}
	}

	FilePath homepath = GetSciteDefaultHome();
	props.Set("SciteDefaultHome", homepath.AsUTF8().c_str());
	homepath = GetSciteUserHome();
	props.Set("SciteUserHome", homepath.AsUTF8().c_str());

	for (size_t i=0; propertiesToForward[i]; i++) {
		ForwardPropertyToEditor(propertiesToForward[i]);
	}

	if (apisFileNames_ != props.GetNewExpandString("api.", fileNameForExtension.c_str())) {
		apis_.Clear();
		ReadAPI(fileNameForExtension);
		apisFileNames_ = props.GetNewExpandString("api.", fileNameForExtension.c_str());
	}

	props.Set("APIPath", apisFileNames_.c_str());

	FilePath fileAbbrev = GUI::StringFromUTF8(props.GetNewExpandString("abbreviations.", fileNameForExtension.c_str()));
	if (!fileAbbrev.IsSet())
		fileAbbrev = GetAbbrevPropertiesFileName();
	if (!pathAbbreviations.SameNameAs(fileAbbrev)) {
		pathAbbreviations = fileAbbrev;
		ReadAbbrevPropFile();
	}

	props.Set("AbbrevPath", pathAbbreviations.AsUTF8().c_str());

	const int tech = props.GetInt("technology");
	wEditor_.Call(SCI_SETTECHNOLOGY, tech);
	wOutput_.Call(SCI_SETTECHNOLOGY, tech);

	const int bidirectional = props.GetInt("bidirectional");
	wEditor_.Call(SCI_SETBIDIRECTIONAL, bidirectional);
	wOutput_.Call(SCI_SETBIDIRECTIONAL, bidirectional);

	codePage_ = props.GetInt("code.page");
	if (CurrentBuffer()->unicodeMode != uni8Bit) {
		// Override properties file to ensure Unicode displayed.
		codePage_ = SC_CP_UTF8;
	}
	wEditor_.Call(SCI_SETCODEPAGE, codePage_);
	const int outputCodePage = props.GetInt("output.code.page", codePage_);
	wOutput_.Call(SCI_SETCODEPAGE, outputCodePage);

	characterSet_ = props.GetInt("character.set", SC_CHARSET_DEFAULT);

#ifdef __unix__
	const std::string localeCType = props.GetString("LC_CTYPE");
	if (localeCType.length())
		setlocale(LC_CTYPE, localeCType.c_str());
	else
		setlocale(LC_CTYPE, "C");
#endif

	std::string imeInteraction = props.GetString("ime.interaction");
	if (imeInteraction.length()) {
		CallChildren(SCI_SETIMEINTERACTION, props.GetInt("ime.interaction", SC_IME_WINDOWED));
	}
	imeAutoComplete = props.GetInt("ime.autocomplete", 0) == 1;

	const int accessibility = props.GetInt("accessibility", 1);
	wEditor_.Call(SCI_SETACCESSIBILITY, accessibility);
	wOutput_.Call(SCI_SETACCESSIBILITY, accessibility);

	wrapStyle = props.GetInt("wrap.style", SC_WRAP_WORD);

	CallChildren(SCI_SETCARETFORE,
	           ColourOfProperty(props, "caret.fore", ColourRGB(0, 0, 0)));

	CallChildren(SCI_SETMOUSESELECTIONRECTANGULARSWITCH, props.GetInt("selection.rectangular.switch.mouse", 0));
	CallChildren(SCI_SETMULTIPLESELECTION, props.GetInt("selection.multiple", 1));
	CallChildren(SCI_SETADDITIONALSELECTIONTYPING, props.GetInt("selection.additional.typing", 1));
	CallChildren(SCI_SETMULTIPASTE, props.GetInt("selection.multipaste", 1));
	CallChildren(SCI_SETADDITIONALCARETSBLINK, props.GetInt("caret.additional.blinks", 1));
	CallChildren(SCI_SETVIRTUALSPACEOPTIONS, props.GetInt("virtual.space"));

	wEditor_.Call(SCI_SETMOUSEDWELLTIME,
	           props.GetInt("dwell.period", SC_TIME_FOREVER), 0);

	wEditor_.Call(SCI_SETCARETWIDTH, props.GetInt("caret.width", 1));
	wOutput_.Call(SCI_SETCARETWIDTH, props.GetInt("caret.width", 1));

	std::string caretLineBack = props.GetExpandedString("caret.line.back");
	if (caretLineBack.length()) {
		wEditor_.Call(SCI_SETCARETLINEVISIBLE, 1);
		wEditor_.Call(SCI_SETCARETLINEBACK, ColourFromString(caretLineBack));
	} else {
		wEditor_.Call(SCI_SETCARETLINEVISIBLE, 0);
	}
	wEditor_.Call(SCI_SETCARETLINEBACKALPHA,
		props.GetInt("caret.line.back.alpha", SC_ALPHA_NOALPHA));

	alphaIndicator = props.GetInt("indicators.alpha", 30);
	if (alphaIndicator < 0 || 255 < alphaIndicator) // If invalid value,
		alphaIndicator = 30; //then set default value.
	underIndicator = props.GetInt("indicators.under", 0) == 1;

	closeFind = static_cast<CloseFind>(props.GetInt("find.close.on.find", 1));

	const std::string controlCharSymbol = props.GetString("control.char.symbol");
	if (controlCharSymbol.length()) {
		wEditor_.Call(SCI_SETCONTROLCHARSYMBOL, static_cast<unsigned char>(controlCharSymbol[0]));
	} else {
		wEditor_.Call(SCI_SETCONTROLCHARSYMBOL, 0);
	}

	const std::string caretPeriod = props.GetString("caret.period");
	if (caretPeriod.length()) {
		wEditor_.Call(SCI_SETCARETPERIOD, atoi(caretPeriod.c_str()));
		wOutput_.Call(SCI_SETCARETPERIOD, atoi(caretPeriod.c_str()));
	}

	int caretSlop = props.GetInt("caret.policy.xslop", 1) ? CARET_SLOP : 0;
	int caretZone = props.GetInt("caret.policy.width", 50);
	int caretStrict = props.GetInt("caret.policy.xstrict") ? CARET_STRICT : 0;
	int caretEven = props.GetInt("caret.policy.xeven", 1) ? CARET_EVEN : 0;
	int caretJumps = props.GetInt("caret.policy.xjumps") ? CARET_JUMPS : 0;
	wEditor_.Call(SCI_SETXCARETPOLICY, caretStrict | caretSlop | caretEven | caretJumps, caretZone);

	caretSlop = props.GetInt("caret.policy.yslop", 1) ? CARET_SLOP : 0;
	caretZone = props.GetInt("caret.policy.lines");
	caretStrict = props.GetInt("caret.policy.ystrict") ? CARET_STRICT : 0;
	caretEven = props.GetInt("caret.policy.yeven", 1) ? CARET_EVEN : 0;
	caretJumps = props.GetInt("caret.policy.yjumps") ? CARET_JUMPS : 0;
	wEditor_.Call(SCI_SETYCARETPOLICY, caretStrict | caretSlop | caretEven | caretJumps, caretZone);

	const int visibleStrict = props.GetInt("visible.policy.strict") ? VISIBLE_STRICT : 0;
	const int visibleSlop = props.GetInt("visible.policy.slop", 1) ? VISIBLE_SLOP : 0;
	const int visibleLines = props.GetInt("visible.policy.lines");
	wEditor_.Call(SCI_SETVISIBLEPOLICY, visibleStrict | visibleSlop, visibleLines);

	wEditor_.Call(SCI_SETEDGECOLUMN, props.GetInt("edge.column", 0));
	wEditor_.Call(SCI_SETEDGEMODE, props.GetInt("edge.mode", EDGE_NONE));
	wEditor_.Call(SCI_SETEDGECOLOUR,
	           ColourOfProperty(props, "edge.colour", ColourRGB(0xff, 0xda, 0xda)));

	std::string selFore = props.GetExpandedString("selection.fore");
	if (selFore.length()) {
		CallChildren(SCI_SETSELFORE, 1, ColourFromString(selFore));
	} else {
		CallChildren(SCI_SETSELFORE, 0, 0);
	}
	std::string selBack = props.GetExpandedString("selection.back");
	if (selBack.length()) {
		CallChildren(SCI_SETSELBACK, 1, ColourFromString(selBack));
	} else {
		if (selFore.length())
			CallChildren(SCI_SETSELBACK, 0, 0);
		else	// Have to show selection somehow
			CallChildren(SCI_SETSELBACK, 1, ColourRGB(0xC0, 0xC0, 0xC0));
	}
	const int selectionAlpha = props.GetInt("selection.alpha", SC_ALPHA_NOALPHA);
	CallChildren(SCI_SETSELALPHA, selectionAlpha);

	std::string selAdditionalFore = props.GetString("selection.additional.fore");
	if (selAdditionalFore.length()) {
		CallChildren(SCI_SETADDITIONALSELFORE, ColourFromString(selAdditionalFore));
	}
	std::string selAdditionalBack = props.GetString("selection.additional.back");
	if (selAdditionalBack.length()) {
		CallChildren(SCI_SETADDITIONALSELBACK, ColourFromString(selAdditionalBack));
	}
	const int selectionAdditionalAlpha = (selectionAlpha == SC_ALPHA_NOALPHA) ? SC_ALPHA_NOALPHA : selectionAlpha / 2;
	CallChildren(SCI_SETADDITIONALSELALPHA, props.GetInt("selection.additional.alpha", selectionAdditionalAlpha));

	foldColour = props.GetExpandedString("fold.margin.colour");
	if (foldColour.length()) {
		CallChildren(SCI_SETFOLDMARGINCOLOUR, 1, ColourFromString(foldColour));
	} else {
		CallChildren(SCI_SETFOLDMARGINCOLOUR, 0, 0);
	}
	foldHiliteColour = props.GetExpandedString("fold.margin.highlight.colour");
	if (foldHiliteColour.length()) {
		CallChildren(SCI_SETFOLDMARGINHICOLOUR, 1, ColourFromString(foldHiliteColour));
	} else {
		CallChildren(SCI_SETFOLDMARGINHICOLOUR, 0, 0);
	}

	std::string whitespaceFore = props.GetExpandedString("whitespace.fore");
	if (whitespaceFore.length()) {
		CallChildren(SCI_SETWHITESPACEFORE, 1, ColourFromString(whitespaceFore));
	} else {
		CallChildren(SCI_SETWHITESPACEFORE, 0, 0);
	}
	std::string whitespaceBack = props.GetExpandedString("whitespace.back");
	if (whitespaceBack.length()) {
		CallChildren(SCI_SETWHITESPACEBACK, 1, ColourFromString(whitespaceBack));
	} else {
		CallChildren(SCI_SETWHITESPACEBACK, 0, 0);
	}

	char bracesStyleKey[200];
	sprintf(bracesStyleKey, "braces.%s.style", language_.c_str());
	bracesStyle = props.GetInt(bracesStyleKey, 0);

	char key[200];
	std::string sval;

	sval = FindLanguageProperty("calltip.*.ignorecase");
	callTipIgnoreCase = sval == "1";
	sval = FindLanguageProperty("calltip.*.use.escapes");
	callTipUseEscapes = sval == "1";

	calltipWordCharacters = FindLanguageProperty("calltip.*.word.characters",
		"_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ");
	calltipParametersStart = FindLanguageProperty("calltip.*.parameters.start", "(");
	calltipParametersEnd = FindLanguageProperty("calltip.*.parameters.end", ")");
	calltipParametersSeparators = FindLanguageProperty("calltip.*.parameters.separators", ",;");

	calltipEndDefinition = FindLanguageProperty("calltip.*.end.definition");

	sprintf(key, "autocomplete.%s.start.characters", language_.c_str());
	autoCompleteStartCharacters = props.GetExpandedString(key);
	if (autoCompleteStartCharacters == "")
		autoCompleteStartCharacters = props.GetExpandedString("autocomplete.*.start.characters");
	// "" is a quite reasonable value for this setting

	sprintf(key, "autocomplete.%s.fillups", language_.c_str());
	autoCompleteFillUpCharacters = props.GetExpandedString(key);
	if (autoCompleteFillUpCharacters == "")
		autoCompleteFillUpCharacters =
			props.GetExpandedString("autocomplete.*.fillups");
	wEditor_.CallString(SCI_AUTOCSETFILLUPS, 0,
		autoCompleteFillUpCharacters.c_str());

	sprintf(key, "autocomplete.%s.typesep", language_.c_str());
	autoCompleteTypeSeparator = props.GetExpandedString(key);
	if (autoCompleteTypeSeparator == "")
		autoCompleteTypeSeparator =
			props.GetExpandedString("autocomplete.*.typesep");
	if (autoCompleteTypeSeparator.length()) {
		wEditor_.Call(SCI_AUTOCSETTYPESEPARATOR,
			static_cast<unsigned char>(autoCompleteTypeSeparator[0]));
	}

	sprintf(key, "autocomplete.%s.ignorecase", "*");
	sval = props.GetNewExpandString(key);
	autoCompleteIgnoreCase = sval == "1";
	sprintf(key, "autocomplete.%s.ignorecase", language_.c_str());
	sval = props.GetNewExpandString(key);
	if (sval != "")
		autoCompleteIgnoreCase = sval == "1";
	wEditor_.Call(SCI_AUTOCSETIGNORECASE, autoCompleteIgnoreCase ? 1 : 0);
	wOutput_.Call(SCI_AUTOCSETIGNORECASE, 1);

	const int autoCChooseSingle = props.GetInt("autocomplete.choose.single");
	wEditor_.Call(SCI_AUTOCSETCHOOSESINGLE, autoCChooseSingle);

	wEditor_.Call(SCI_AUTOCSETCANCELATSTART, 0);
	wEditor_.Call(SCI_AUTOCSETDROPRESTOFWORD, 0);

	if (firstPropertiesRead) {
		ReadPropertiesInitial();
	}

	ReadFontProperties();

	wEditor_.Call(SCI_SETPRINTMAGNIFICATION, props.GetInt("print.magnification"));
	wEditor_.Call(SCI_SETPRINTCOLOURMODE, props.GetInt("print.colour.mode"));

	jobQueue.clearBeforeExecute = props.GetInt("clear.before.execute");
	jobQueue.timeCommands = props.GetInt("time.commands");

	const int blankMarginLeft = props.GetInt("blank.margin.left", 1);
	const int blankMarginLeftOutput = props.GetInt("output.blank.margin.left", blankMarginLeft);
	const int blankMarginRight = props.GetInt("blank.margin.right", 1);
	wEditor_.Call(SCI_SETMARGINLEFT, 0, blankMarginLeft);
	wEditor_.Call(SCI_SETMARGINRIGHT, 0, blankMarginRight);
	wOutput_.Call(SCI_SETMARGINLEFT, 0, blankMarginLeftOutput);
	wOutput_.Call(SCI_SETMARGINRIGHT, 0, blankMarginRight);

	marginWidth = props.GetInt("margin.width");
	if (marginWidth == 0)
		marginWidth = kMarginWidthDefault;
	wEditor_.Call(SCI_SETMARGINWIDTHN, 1, margin ? marginWidth : 0);

	const std::string lineMarginProp = props.GetString("line.margin.width");
	lineNumbersWidth = atoi(lineMarginProp.c_str());
	if (lineNumbersWidth == 0)
		lineNumbersWidth = kLineNumbersWidthDefault;
	lineNumbersExpand = lineMarginProp.find('+') != std::string::npos;

	SetLineNumberWidth();

	bufferedDraw = props.GetInt("buffered.draw");
	wEditor_.Call(SCI_SETBUFFEREDDRAW, bufferedDraw);
	wOutput_.Call(SCI_SETBUFFEREDDRAW, bufferedDraw);

	const int phasesDraw = props.GetInt("phases.draw", 1);
	wEditor_.Call(SCI_SETPHASESDRAW, phasesDraw);
	wOutput_.Call(SCI_SETPHASESDRAW, phasesDraw);

	wEditor_.Call(SCI_SETLAYOUTCACHE, props.GetInt("cache.layout", SC_CACHE_CARET));
	wOutput_.Call(SCI_SETLAYOUTCACHE, props.GetInt("output.cache.layout", SC_CACHE_CARET));

	bracesCheck = props.GetInt("braces.check");
	bracesSloppy = props.GetInt("braces.sloppy");

	wEditor_.Call(SCI_SETCHARSDEFAULT);
	wordCharacters = props.GetNewExpandString("word.characters.", fileNameForExtension.c_str());
	if (wordCharacters.length()) {
		wEditor_.CallString(SCI_SETWORDCHARS, 0, wordCharacters.c_str());
	} else {
		wordCharacters = "_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	}

	whitespaceCharacters = props.GetNewExpandString("whitespace.characters.", fileNameForExtension.c_str());
	if (whitespaceCharacters.length()) {
		wEditor_.CallString(SCI_SETWHITESPACECHARS, 0, whitespaceCharacters.c_str());
	}

	const std::string viewIndentExamine = GetFileNameProperty("view.indentation.examine");
	indentExamine = viewIndentExamine.length() ? (atoi(viewIndentExamine.c_str())) : SC_IV_REAL;
	wEditor_.Call(SCI_SETINDENTATIONGUIDES, props.GetInt("view.indentation.guides") ?
		indentExamine : SC_IV_NONE);

	wEditor_.Call(SCI_SETTABINDENTS, props.GetInt("tab.indents", 1));
	wEditor_.Call(SCI_SETBACKSPACEUNINDENTS, props.GetInt("backspace.unindents", 1));

	wEditor_.Call(SCI_CALLTIPUSESTYLE, 32);

	std::string useStripTrailingSpaces = props.GetNewExpandString("strip.trailing.spaces.", ExtensionFileName().c_str());
	if (useStripTrailingSpaces.length() > 0) {
		stripTrailingSpaces_ = atoi(useStripTrailingSpaces.c_str()) != 0;
	} else {
		stripTrailingSpaces_ = props.GetInt("strip.trailing.spaces") != 0;
	}
	ensureFinalLineEnd_ = props.GetInt("ensure.final.line.end") != 0;
	ensureConsistentLineEnds_ = props.GetInt("ensure.consistent.line.ends") != 0;

	indentOpening_ = props.GetInt("indent.opening");
	indentClosing_ = props.GetInt("indent.closing");
	indentMaintain_ = atoi(props.GetNewExpandString("indent.maintain.", fileNameForExtension.c_str()).c_str());

	const std::string lookback = props.GetNewExpandString("statement.lookback.", fileNameForExtension.c_str());
	statementLookback_ = atoi(lookback.c_str());
	statementIndent_ = GetStyleAndWords("statement.indent.");
	statementEnd_ = GetStyleAndWords("statement.end.");
	blockStart_ = GetStyleAndWords("block.start.");
	blockEnd_ = GetStyleAndWords("block.end.");

	struct PropToPPC {
		const char *propName;
		PreProcKind ppc;
	};
	PropToPPC propToPPC[] = {
		{"preprocessor.start.", kPpcStart},
		{"preprocessor.middle.", kPpcMiddle},
		{"preprocessor.end.", kPpcEnd},
	};
	const std::string ppSymbol = props.GetNewExpandString("preprocessor.symbol.", fileNameForExtension.c_str());
	preprocessorSymbol_ = ppSymbol.empty() ? 0 : ppSymbol[0];
	preprocOfString_.clear();
	for (const PropToPPC &preproc : propToPPC) {
		const std::string list = props.GetNewExpandString(preproc.propName, fileNameForExtension.c_str());
		const std::vector<std::string> words = StringSplit(list, ' ');
		for (const std::string &word : words) {
			preprocOfString_[word] = preproc.ppc;
		}
	}

	memFiles_.AppendList(props.GetNewExpandString("find.files").c_str());

	wEditor_.Call(SCI_SETWRAPVISUALFLAGS, props.GetInt("wrap.visual.flags"));
	wEditor_.Call(SCI_SETWRAPVISUALFLAGSLOCATION, props.GetInt("wrap.visual.flags.location"));
 	wEditor_.Call(SCI_SETWRAPSTARTINDENT, props.GetInt("wrap.visual.startindent"));
 	wEditor_.Call(SCI_SETWRAPINDENTMODE, props.GetInt("wrap.indent.mode"));

	wEditor_.Call(SCI_SETIDLESTYLING, props.GetInt("idle.styling", SC_IDLESTYLING_NONE));
	wOutput_.Call(SCI_SETIDLESTYLING, props.GetInt("output.idle.styling", SC_IDLESTYLING_NONE));

	if (props.GetInt("os.x.home.end.keys")) {
		AssignKey(SCK_HOME, 0, SCI_SCROLLTOSTART);
		AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_NULL);
		AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_NULL);
		AssignKey(SCK_END, 0, SCI_SCROLLTOEND);
		AssignKey(SCK_END, SCMOD_SHIFT, SCI_NULL);
	} else {
		if (props.GetInt("wrap.aware.home.end.keys",0)) {
			if (props.GetInt("vc.home.key", 1)) {
				AssignKey(SCK_HOME, 0, SCI_VCHOMEWRAP);
				AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_VCHOMEWRAPEXTEND);
				AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_VCHOMERECTEXTEND);
			} else {
				AssignKey(SCK_HOME, 0, SCI_HOMEWRAP);
				AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_HOMEWRAPEXTEND);
				AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_HOMERECTEXTEND);
			}
			AssignKey(SCK_END, 0, SCI_LINEENDWRAP);
			AssignKey(SCK_END, SCMOD_SHIFT, SCI_LINEENDWRAPEXTEND);
		} else {
			if (props.GetInt("vc.home.key", 1)) {
				AssignKey(SCK_HOME, 0, SCI_VCHOME);
				AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_VCHOMEEXTEND);
				AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_VCHOMERECTEXTEND);
			} else {
				AssignKey(SCK_HOME, 0, SCI_HOME);
				AssignKey(SCK_HOME, SCMOD_SHIFT, SCI_HOMEEXTEND);
				AssignKey(SCK_HOME, SCMOD_SHIFT | SCMOD_ALT, SCI_HOMERECTEXTEND);
			}
			AssignKey(SCK_END, 0, SCI_LINEEND);
			AssignKey(SCK_END, SCMOD_SHIFT, SCI_LINEENDEXTEND);
		}
	}

	AssignKey('L', SCMOD_SHIFT | SCMOD_CTRL, SCI_LINEDELETE);


	scrollOutput = props.GetInt("output.scroll", 1);

	tabHideOne = props.GetInt("tabbar.hide.one");

	SetToolsMenu();

	wEditor_.Call(SCI_SETFOLDFLAGS, props.GetInt("fold.flags"));

	// To put the folder markers in the line number region
	//wEditor_.Call(SCI_SETMARGINMASKN, 0, SC_MASK_FOLDERS);

	wEditor_.Call(SCI_SETMODEVENTMASK, SC_MOD_CHANGEFOLD);

	if (0==props.GetInt("undo.redo.lazy")) {
		// Trap for insert/delete notifications (also fired by undo
		// and redo) so that the buttons can be enabled if needed.
		wEditor_.Call(SCI_SETMODEVENTMASK, SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT
			| SC_LASTSTEPINUNDOREDO | wEditor_.Call(SCI_GETMODEVENTMASK, 0));

		//SC_LASTSTEPINUNDOREDO is probably not needed in the mask; it
		//doesn't seem to fire as an event of its own; just modifies the
		//insert and delete events.
	}

	// Create a margin column for the folding symbols
	wEditor_.Call(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);

	foldMarginWidth = props.GetInt("fold.margin.width");
	if (foldMarginWidth == 0)
		foldMarginWidth = kFoldMarginWidthDefault;
	wEditor_.Call(SCI_SETMARGINWIDTHN, 2, foldMargin ? foldMarginWidth : 0);

	wEditor_.Call(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
	wEditor_.Call(SCI_SETMARGINSENSITIVEN, 2, 1);

	// Define foreground (outline) and background (fill) color of folds
	const int foldSymbols = props.GetInt("fold.symbols");
	std::string foldFore = props.GetExpandedString("fold.fore");
	if (foldFore.length() == 0) {
		// Set default colour for outline
		switch (foldSymbols) {
		case 0: // Arrows
			foldFore = "#000000";
			break;
		case 1: // + -
			foldFore = "#FFFFFF";
			break;
		case 2: // Circles
			foldFore = "#404040";
			break;
		case 3: // Squares
			foldFore = "#808080";
			break;
		}
	}
	const Colour colourFoldFore = ColourFromString(foldFore);

	std::string foldBack = props.GetExpandedString("fold.back");
	// Set default colour for fill
	if (foldBack.length() == 0) {
		switch (foldSymbols) {
		case 0:
		case 1:
			foldBack = "#000000";
			break;
		case 2:
		case 3:
			foldBack = "#FFFFFF";
			break;
		}
	}
	const Colour colourFoldBack = ColourFromString(foldBack);

	// Enable/disable highlight for current folding block (smallest one that contains the caret)
	const int isHighlightEnabled = props.GetInt("fold.highlight", 0);
	// Define the colour of highlight
	const Colour colourFoldBlockHighlight = ColourOfProperty(props, "fold.highlight.colour", ColourRGB(0xFF, 0, 0));

	switch (foldSymbols) {
	case 0:
		// Arrow pointing right for contracted folders, arrow pointing down for expanded
		DefineMarker(SC_MARKNUM_FOLDEROPEN, SC_MARK_ARROWDOWN,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDER, SC_MARK_ARROW,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERSUB, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERTAIL, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEREND, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEROPENMID, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_EMPTY,
					 colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		// The highlight is disabled for arrow.
		wEditor_.Call(SCI_MARKERENABLEHIGHLIGHT, false);
		break;
	case 1:
		// Plus for contracted folders, minus for expanded
		DefineMarker(SC_MARKNUM_FOLDEROPEN, SC_MARK_MINUS,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDER, SC_MARK_PLUS,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERSUB, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERTAIL, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEREND, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEROPENMID, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_EMPTY,
		             colourFoldFore, colourFoldBack, colourFoldBlockHighlight);
		// The highlight is disabled for plus/minus.
		wEditor_.Call(SCI_MARKERENABLEHIGHLIGHT, false);
		break;
	case 2:
		// Like a flattened tree control using circular headers and curved joins
		DefineMarker(SC_MARKNUM_FOLDEROPEN, SC_MARK_CIRCLEMINUS,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDER, SC_MARK_CIRCLEPLUS,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNERCURVE,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEREND, SC_MARK_CIRCLEPLUSCONNECTED,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEROPENMID, SC_MARK_CIRCLEMINUSCONNECTED,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNERCURVE,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		wEditor_.Call(SCI_MARKERENABLEHIGHLIGHT, isHighlightEnabled);
		break;
	case 3:
		// Like a flattened tree control using square headers
		DefineMarker(SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		DefineMarker(SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER,
		             colourFoldBack, colourFoldFore, colourFoldBlockHighlight);
		wEditor_.Call(SCI_MARKERENABLEHIGHLIGHT, isHighlightEnabled);
		break;
	}

	wEditor_.Call(SCI_MARKERSETFORE, kMarkerBookmark,
		ColourOfProperty(props, "bookmark.fore", ColourRGB(0xbe, 0, 0)));
	wEditor_.Call(SCI_MARKERSETBACK, kMarkerBookmark,
		ColourOfProperty(props, "bookmark.back", ColourRGB(0xe2, 0x40, 0x40)));
	wEditor_.Call(SCI_MARKERSETALPHA, kMarkerBookmark,
		props.GetInt("bookmark.alpha", SC_ALPHA_NOALPHA));
	const std::string bookMarkXPM = props.GetString("bookmark.pixmap");
	if (bookMarkXPM.length()) {
		wEditor_.CallString(SCI_MARKERDEFINEPIXMAP, kMarkerBookmark,
			bookMarkXPM.c_str());
	} else if (props.GetString("bookmark.fore").length()) {
		wEditor_.Call(SCI_MARKERDEFINE, kMarkerBookmark, props.GetInt("bookmark.symbol", SC_MARK_BOOKMARK));
	} else {
		// No bookmark.fore setting so display default pixmap.
		wEditor_.CallPointer(SCI_MARKERDEFINEPIXMAP, kMarkerBookmark, bookmarkBluegem);
	}

	wEditor_.Call(SCI_SETSCROLLWIDTH, props.GetInt("horizontal.scroll.width", 2000));
	wEditor_.Call(SCI_SETSCROLLWIDTHTRACKING, props.GetInt("horizontal.scroll.width.tracking", 1));
	wOutput_.Call(SCI_SETSCROLLWIDTH, props.GetInt("output.horizontal.scroll.width", 2000));
	wOutput_.Call(SCI_SETSCROLLWIDTHTRACKING, props.GetInt("output.horizontal.scroll.width.tracking", 1));

	// Do these last as they force a style refresh
	wEditor_.Call(SCI_SETHSCROLLBAR, props.GetInt("horizontal.scrollbar", 1));
	wOutput_.Call(SCI_SETHSCROLLBAR, props.GetInt("output.horizontal.scrollbar", 1));

	wEditor_.Call(SCI_SETENDATLASTLINE, props.GetInt("end.at.last.line", 1));
	wEditor_.Call(SCI_SETCARETSTICKY, props.GetInt("caret.sticky", 0));

	// Clear all previous indicators.
	wEditor_.Call(SCI_SETINDICATORCURRENT, kIndicatorHighlightCurrentWord);
	wEditor_.Call(SCI_INDICATORCLEARRANGE, 0, wEditor_.Call(SCI_GETLENGTH));
	wOutput_.Call(SCI_SETINDICATORCURRENT, kIndicatorHighlightCurrentWord);
	wOutput_.Call(SCI_INDICATORCLEARRANGE, 0, wOutput_.Call(SCI_GETLENGTH));
	currentWordHighlight.statesOfDelay = currentWordHighlight.kNoDelay;

	currentWordHighlight.isEnabled = props.GetInt("highlight.current.word", 0) == 1;
	if (currentWordHighlight.isEnabled) {
		const std::string highlightCurrentWordIndicatorString = props.GetExpandedString("highlight.current.word.indicator");
		IndicatorDefinition highlightCurrentWordIndicator(highlightCurrentWordIndicatorString.c_str());
		if (highlightCurrentWordIndicatorString.length() == 0) {
			highlightCurrentWordIndicator.style = INDIC_ROUNDBOX;
			std::string highlightCurrentWordColourString = props.GetExpandedString("highlight.current.word.colour");
			if (highlightCurrentWordColourString.length() == 0) {
				// Set default colour for highlight.
				highlightCurrentWordColourString = "#A0A000";
			}
			highlightCurrentWordIndicator.colour = ColourFromString(highlightCurrentWordColourString);
			highlightCurrentWordIndicator.fillAlpha = alphaIndicator;
			highlightCurrentWordIndicator.under = underIndicator;
		}
		SetOneIndicator(wEditor_, kIndicatorHighlightCurrentWord, highlightCurrentWordIndicator);
		SetOneIndicator(wOutput_, kIndicatorHighlightCurrentWord, highlightCurrentWordIndicator);
		currentWordHighlight.isOnlyWithSameStyle = props.GetInt("highlight.current.word.by.style", 0) == 1;
		HighlightCurrentWord(true);
	}

	std::map<std::string, std::string> eConfig = editorConfig->MapFromAbsolutePath(filePath_);
	for (const std::pair<const std::string, std::string> &pss : eConfig) {
		if (pss.first == "indent_style") {
			wEditor_.Call(SCI_SETUSETABS, pss.second == "tab" ? 1 : 0);
		} else if (pss.first == "indent_size") {
			wEditor_.Call(SCI_SETINDENT, std::stoi(pss.second));
		} else if (pss.first == "tab_width") {
			wEditor_.Call(SCI_SETTABWIDTH, std::stoi(pss.second));
		} else if (pss.first == "end_of_line") {
			if (pss.second == "lf") {
				wEditor_.Call(SCI_SETEOLMODE, SC_EOL_LF);
			} else if (pss.second == "cr") {
				wEditor_.Call(SCI_SETEOLMODE, SC_EOL_CR);
			} else if (pss.second == "crlf") {
				wEditor_.Call(SCI_SETEOLMODE, SC_EOL_CRLF);
			}
		} else if (pss.first == "charset") {
			if (pss.second == "latin1") {
				CurrentBuffer()->unicodeMode = uni8Bit;
				codePage_ = 0;
			} else {
				if (pss.second == "utf-8")
					CurrentBuffer()->unicodeMode = uniCookie;
				if (pss.second == "utf-8-bom")
					CurrentBuffer()->unicodeMode = uniUTF8;
				if (pss.second == "utf-16be")
					CurrentBuffer()->unicodeMode = uni16BE;
				if (pss.second == "utf-16le")
					CurrentBuffer()->unicodeMode = uni16LE;
				codePage_ = SC_CP_UTF8;
			}
			wEditor_.Call(SCI_SETCODEPAGE, codePage_);
		} else if (pss.first == "trim_trailing_whitespace") {
			stripTrailingSpaces_ = pss.second == "true";
		} else if (pss.first == "insert_final_newline") {
			ensureFinalLineEnd_ = pss.second == "true";
		}
	}

	if (extender) {
		FilePath defaultDir = GetDefaultDirectory();
		FilePath scriptPath;

		// Check for an extension script
		GUI::gui_string extensionFile = GUI::StringFromUTF8(
			props.GetNewExpandString("extension.", fileNameForExtension.c_str()));
		if (extensionFile.length()) {
			// find file in local directory
			FilePath docDir = filePath_.Directory();
			if (Exists(docDir.AsInternal(), extensionFile.c_str(), &scriptPath)) {
				// Found file in document directory
				extender->Load(scriptPath.AsUTF8().c_str());
			} else if (Exists(defaultDir.AsInternal(), extensionFile.c_str(), &scriptPath)) {
				// Found file in global directory
				extender->Load(scriptPath.AsUTF8().c_str());
			} else if (Exists(GUI_TEXT(""), extensionFile.c_str(), &scriptPath)) {
				// Found as completely specified file name
				extender->Load(scriptPath.AsUTF8().c_str());
			}
		}
	}

	delayBeforeAutoSave = props.GetInt("save.on.timer");
	if (delayBeforeAutoSave) {
		TimerStart(kTimerAutoSave);
	} else {
		TimerEnd(kTimerAutoSave);
	}

	firstPropertiesRead = false;
	needReadProperties = false;
}

void SciTEBase::ReadFontProperties() {
	char key[200];
	const char *languageName = language_.c_str();

	if (lexLanguage_ == lexLPeg_) {
		// Retrieve style info.
		char propStr[256];
		for (int i = 0; i < STYLE_MAX; i++) {
			sprintf(key, "style.lpeg.%0d", i);
			wEditor_.CallReturnPointer(SCI_PRIVATELEXERCALL, i - STYLE_MAX,
				SptrFromString(propStr));
			props.Set(key, static_cast<const char *>(propStr));
		}
		languageName = "lpeg";
	}

	// Set styles
	// For each window set the global default style, then the language default style, then the other global styles, then the other language styles

	const int fontQuality = props.GetInt("font.quality");
	wEditor_.Call(SCI_SETFONTQUALITY, fontQuality);
	wOutput_.Call(SCI_SETFONTQUALITY, fontQuality);

	wEditor_.Call(SCI_STYLERESETDEFAULT, 0, 0);
	wOutput_.Call(SCI_STYLERESETDEFAULT, 0, 0);

	sprintf(key, "style.%s.%0d", "*", STYLE_DEFAULT);
	std::string sval = props.GetNewExpandString(key);
	SetOneStyle(wEditor_, STYLE_DEFAULT, StyleDefinition(sval));
	SetOneStyle(wOutput_, STYLE_DEFAULT, StyleDefinition(sval));

	sprintf(key, "style.%s.%0d", languageName, STYLE_DEFAULT);
	sval = props.GetNewExpandString(key);
	SetOneStyle(wEditor_, STYLE_DEFAULT, StyleDefinition(sval));

	wEditor_.Call(SCI_STYLECLEARALL, 0, 0);

	SetStyleFor(wEditor_, "*");
	SetStyleFor(wEditor_, languageName);
	if (props.GetInt("error.inline")) {
		wEditor_.Call(SCI_RELEASEALLEXTENDEDSTYLES, 0, 0);
		diagnosticStyleStart_ = wEditor_.Call(SCI_ALLOCATEEXTENDEDSTYLES, kDiagnosticStyles, 0);
		SetStyleBlock(wEditor_, "error", diagnosticStyleStart_, diagnosticStyleStart_+kDiagnosticStyles-1);
	}

	const int diffToSecondary = static_cast<int>(wEditor_.Call(SCI_DISTANCETOSECONDARYSTYLES));
	for (const char subStyleBase : subStyleBases_) {
		const int subStylesStart = wEditor_.Call(SCI_GETSUBSTYLESSTART, subStyleBase);
		const int subStylesLength = wEditor_.Call(SCI_GETSUBSTYLESLENGTH, subStyleBase);
		for (int subStyle=0; subStyle<subStylesLength; subStyle++) {
			for (int active=0; active<(diffToSecondary?2:1); active++) {
				const int activity = active * diffToSecondary;
				sprintf(key, "style.%s.%0d.%0d", languageName, subStyleBase + activity, subStyle+1);
				sval = props.GetNewExpandString(key);
				SetOneStyle(wEditor_, subStylesStart + subStyle + activity, StyleDefinition(sval));
			}
		}
	}

	// Turn grey while loading
	if (CurrentBuffer()->lifeState == Buffer::kReading)
		wEditor_.Call(SCI_STYLESETBACK, STYLE_DEFAULT, 0xEEEEEE);

	wOutput_.Call(SCI_STYLECLEARALL, 0, 0);

	sprintf(key, "style.%s.%0d", "errorlist", STYLE_DEFAULT);
	sval = props.GetNewExpandString(key);
	SetOneStyle(wOutput_, STYLE_DEFAULT, StyleDefinition(sval));

	wOutput_.Call(SCI_STYLECLEARALL, 0, 0);

	SetStyleFor(wOutput_, "*");
	SetStyleFor(wOutput_, "errorlist");

	if (CurrentBuffer()->useMonoFont) {
		sval = props.GetExpandedString("font.monospace");
		StyleDefinition sd(sval.c_str());
		for (int style = 0; style <= STYLE_MAX; style++) {
			if (style != STYLE_LINENUMBER) {
				if (sd.specified & StyleDefinition::sdFont) {
					wEditor_.CallString(SCI_STYLESETFONT, style, sd.font.c_str());
				}
				if (sd.specified & StyleDefinition::sdSize) {
					wEditor_.Call(SCI_STYLESETSIZEFRACTIONAL, style, sd.FractionalSize());
				}
			}
		}
	}
}

// Properties that are interactively modifiable are only read from the properties file once.
void SciTEBase::SetPropertiesInitial() {
	splitVertical = props.GetInt("split.vertical");
	openFilesHere = props.GetInt("check.if.already.open");
	wrap = props.GetInt("wrap");
	wrapOutput = props.GetInt("output.wrap");
	indentationWSVisible_ = props.GetInt("view.indentation.whitespace", 1);
	sbVisible = props.GetInt("statusbar.visible");
	tbVisible = props.GetInt("toolbar.visible");
	tabVisible = props.GetInt("tabbar.visible");
	tabMultiLine = props.GetInt("tabbar.multiline");
	lineNumbers = props.GetInt("line.margin.visible");
	margin = props.GetInt("margin.width");
	foldMargin = props.GetInt("fold.margin.width", kFoldMarginWidthDefault);

	matchCase = props.GetInt("find.replace.matchcase");
	regExp = props.GetInt("find.replace.regexp");
	unSlash = props.GetInt("find.replace.escapes");
	wrapFind = props.GetInt("find.replace.wrap", 1);
	focusOnReplace = props.GetInt("find.replacewith.focus", 1);
}

GUI::gui_string Localization::Text(const char *s, bool retainIfNotFound) {
	const std::string sEllipse("...");	// An ASCII ellipse
	const std::string utfEllipse("\xe2\x80\xa6");	// A UTF-8 ellipse
	std::string translation = s;
	const int ellipseIndicator = Remove(translation, sEllipse);
	const int utfEllipseIndicator = Remove(translation, utfEllipse);
	const std::string menuAccessIndicatorChar(1, static_cast<char>(menuAccessIndicator[0]));
	const int accessKeyPresent = Remove(translation, menuAccessIndicatorChar);
	LowerCaseAZ(translation);
	Substitute(translation, "\n", "\\n");
	translation = GetString(translation.c_str());
	if (translation.length()) {
		if (ellipseIndicator)
			translation += sEllipse;
		if (utfEllipseIndicator)
			translation += utfEllipse;
		if (0 == accessKeyPresent) {
#if !defined(GTK)
			// Following codes are required because accelerator is not always
			// part of alphabetical word in several language. In these cases,
			// accelerator is written like "(&O)".
			const size_t posOpenParenAnd = translation.find("(&");
			if ((posOpenParenAnd != std::string::npos) && (translation.find(")", posOpenParenAnd) == posOpenParenAnd + 3)) {
				translation.erase(posOpenParenAnd, 4);
			} else {
				Remove(translation, std::string("&"));
			}
#else
			Remove(translation, std::string("&"));
#endif
		}
		Substitute(translation, "&", menuAccessIndicatorChar);
		Substitute(translation, "\\n", "\n");
	} else {
		translation = missing;
	}
	if ((translation.length() > 0) || !retainIfNotFound) {
		return GUI::StringFromUTF8(translation);
	}
	return GUI::StringFromUTF8(s);
}

GUI::gui_string SciTEBase::LocaliseMessage(const char *s, const GUI::gui_char *param0, const GUI::gui_char *param1, const GUI::gui_char *param2) {
	GUI::gui_string translation = localiser.Text(s);
	if (param0)
		Substitute(translation, GUI_TEXT("^0"), param0);
	if (param1)
		Substitute(translation, GUI_TEXT("^1"), param1);
	if (param2)
		Substitute(translation, GUI_TEXT("^2"), param2);
	return translation;
}

void SciTEBase::ReadLocalization() {
	localiser.Clear();
	GUI::gui_string title = GUI_TEXT("locale.properties");
	const std::string localeProps = props.GetExpandedString("locale.properties");
	if (localeProps.length()) {
		title = GUI::StringFromUTF8(localeProps);
	}
	FilePath propdir = GetSciteDefaultHome();
	FilePath localePath(propdir, title);
	localiser.Read(localePath, propdir, filter_, &importFiles_, 0);
	localiser.SetMissing(props.GetString("translation.missing"));
	localiser.read = true;
}

void SciTEBase::ReadPropertiesInitial() {
	SetPropertiesInitial();
	const int sizeHorizontal = props.GetInt("output.horizontal.size", 0);
	const int sizeVertical = props.GetInt("output.vertical.size", 0);
	const int hideOutput = props.GetInt("output.initial.hide", 0);
	if ((!splitVertical && (sizeVertical > 0) && (heightOutput < sizeVertical)) ||
		(splitVertical && (sizeHorizontal > 0) && (heightOutput < sizeHorizontal))) {
		previousHeightOutput = splitVertical ? sizeHorizontal : sizeVertical;
		if (!hideOutput) {
			heightOutput = NormaliseSplit(previousHeightOutput);
			SizeSubWindows();
			Redraw();
		}
	}
	ViewWhitespace(props.GetInt("view.whitespace"));
	wEditor_.Call(SCI_SETINDENTATIONGUIDES, props.GetInt("view.indentation.guides") ?
		indentExamine : SC_IV_NONE);

	wEditor_.Call(SCI_SETVIEWEOL, props.GetInt("view.eol"));
	wEditor_.Call(SCI_SETZOOM, props.GetInt("magnification"));
	wOutput_.Call(SCI_SETZOOM, props.GetInt("output.magnification"));
	wEditor_.Call(SCI_SETWRAPMODE, wrap ? wrapStyle : SC_WRAP_NONE);
	wOutput_.Call(SCI_SETWRAPMODE, wrapOutput ? wrapStyle : SC_WRAP_NONE);

	std::string menuLanguageProp = props.GetExpandedString("menu.language");
	std::replace(menuLanguageProp.begin(), menuLanguageProp.end(), '|', '\0');
	const char *sMenuLanguage = menuLanguageProp.c_str();
	while (*sMenuLanguage) {
		LanguageMenuItem lmi;
		lmi.menuItem = sMenuLanguage;
		sMenuLanguage += strlen(sMenuLanguage) + 1;
		lmi.extension = sMenuLanguage;
		sMenuLanguage += strlen(sMenuLanguage) + 1;
		lmi.menuKey = sMenuLanguage;
		sMenuLanguage += strlen(sMenuLanguage) + 1;
		languageMenu_.push_back(lmi);
	}
	SetLanguageMenu();

	// load the user defined short cut props
	std::string shortCutProp = props.GetNewExpandString("user.shortcuts");
	if (shortCutProp.length()) {
		const size_t pipes = std::count(shortCutProp.begin(), shortCutProp.end(), '|');
		std::replace(shortCutProp.begin(), shortCutProp.end(), '|', '\0');
		const char *sShortCutProp = shortCutProp.c_str();
		for (size_t item = 0; item < pipes/2; item++) {
			ShortcutItem sci;
			sci.menuKey = sShortCutProp;
			sShortCutProp += strlen(sShortCutProp) + 1;
			sci.menuCommand = sShortCutProp;
			sShortCutProp += strlen(sShortCutProp) + 1;
			shortCutItemList_.push_back(sci);
		}
	}
	// end load the user defined short cut props

	FilePath homepath = GetSciteDefaultHome();
	props.Set("SciteDefaultHome", homepath.AsUTF8().c_str());
	homepath = GetSciteUserHome();
	props.Set("SciteUserHome", homepath.AsUTF8().c_str());
}

FilePath SciTEBase::GetDefaultPropertiesFileName() {
	return FilePath(GetSciteDefaultHome(), propGlobalFileName);
}

FilePath SciTEBase::GetAbbrevPropertiesFileName() {
	return FilePath(GetSciteUserHome(), propAbbrevFileName);
}

FilePath SciTEBase::GetUserPropertiesFileName() {
	return FilePath(GetSciteUserHome(), propUserFileName);
}

FilePath SciTEBase::GetLocalPropertiesFileName() {
	return FilePath(filePath_.Directory(), propLocalFileName);
}

FilePath SciTEBase::GetDirectoryPropertiesFileName() {
	FilePath propfile;

	if (filePath_.IsSet()) {
		propfile.Set(filePath_.Directory(), propDirectoryFileName);

		// if this file does not exist try to find the prop file in a parent directory
		while (!propfile.Directory().IsRoot() && !propfile.Exists()) {
			propfile.Set(propfile.Directory().Directory(), propDirectoryFileName);
		}

		// not found -> set it to the initial directory
		if (!propfile.Exists()) {
			propfile.Set(filePath_.Directory(), propDirectoryFileName);
		}
	}
	return propfile;
}

void SciTEBase::OpenProperties(int propsFile) {
	FilePath propfile;
	switch (propsFile) {
	case IDM_OPENLOCALPROPERTIES:
		propfile = GetLocalPropertiesFileName();
		Open(propfile, kOfQuiet);
		break;
	case IDM_OPENUSERPROPERTIES:
		propfile = GetUserPropertiesFileName();
		Open(propfile, kOfQuiet);
		break;
	case IDM_OPENABBREVPROPERTIES:
		propfile = pathAbbreviations;
		Open(propfile, kOfQuiet);
		break;
	case IDM_OPENGLOBALPROPERTIES:
		propfile = GetDefaultPropertiesFileName();
		Open(propfile, kOfQuiet);
		break;
	case IDM_OPENLUAEXTERNALFILE: {
			GUI::gui_string extlua = GUI::StringFromUTF8(props.GetExpandedString("ext.lua.startup.script"));
			if (extlua.length()) {
				Open(extlua.c_str(), kOfQuiet);
			}
			break;
		}
	case IDM_OPENDIRECTORYPROPERTIES: {
			propfile = GetDirectoryPropertiesFileName();
			const bool alreadyExists = propfile.Exists();
			Open(propfile, kOfQuiet);
			if (!alreadyExists)
				SaveAsDialog();
		}
		break;
	}
}

// return the int value of the command name passed in.
int SciTEBase::GetMenuCommandAsInt(std::string commandName) {
	int i = IFaceTable::FindConstant(commandName.c_str());
	if (i != -1) {
		return IFaceTable::constants[i].value;
	}

	// Check also for a SCI command, as long as it has no parameters
	i = IFaceTable::FindFunctionByConstantName(commandName.c_str());
	if (i != -1 &&
		IFaceTable::functions[i].paramType[0] == iface_void &&
		IFaceTable::functions[i].paramType[1] == iface_void) {
		return IFaceTable::functions[i].value;
	}

	// Otherwise we might have entered a number as command to access a "SCI_" command
	return atoi(commandName.c_str());
}
