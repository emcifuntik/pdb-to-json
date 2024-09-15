// main.cpp

#include <Windows.h>
#include <dia2.h>
#include <atlbase.h> // For CComPtr
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint> // For uintptr_t
#include <sstream> // For std::wstringstream
#include <iomanip> // For std::setprecision
#include <mutex>   // For std::mutex

// Include the nlohmann/json library
#include "json.hpp"

using json = nlohmann::json;

// Link against the DIA SDK library
#pragma comment(lib, "diaguids.lib")

// Function prototypes
void EnumerateSymbols(CComPtr<IDiaSymbol> pGlobal, json& output, const std::wstring& filePrefix);
void ProcessUDT(CComPtr<IDiaSymbol> pSymbol, json& classesArray, const std::wstring& filePrefix, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage);
void ProcessEnum(CComPtr<IDiaSymbol> pSymbol, json& enumsArray, const std::wstring& filePrefix, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage);
void ProcessTypedef(CComPtr<IDiaSymbol> pSymbol, json& typedefsArray, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage);
void ProcessFunction(CComPtr<IDiaSymbol> pSymbol, json& functionsArray, const std::wstring& filePrefix, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage);
void ProcessData(CComPtr<IDiaSymbol> pSymbol, json& globalsArray, const std::wstring& filePrefix, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage);

std::wstring GetSymbolName(CComPtr<IDiaSymbol> pSymbol);
std::wstring GetSymbolFileName(CComPtr<IDiaSymbol> pSymbol);
DWORD GetSymbolLineNumber(CComPtr<IDiaSymbol> pSymbol);
std::wstring GetUndecoratedName(CComPtr<IDiaSymbol> pSymbol);
std::wstring GetTypeName(CComPtr<IDiaSymbol> pType);
std::wstring GetBasicTypeName(DWORD baseType, DWORD length);

std::string WStringToString(const std::wstring& wstr) {
	if (wstr.empty())
		return std::string();

	int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), NULL, 0, NULL, NULL);
	if (sizeNeeded <= 0)
		return std::string();

	std::string strTo(sizeNeeded, 0);
	int bytesConverted = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &strTo[0], sizeNeeded, NULL, NULL);
	if (bytesConverted != sizeNeeded)
		return std::string();

	return strTo;
}

// Global type name cache to optimize GetTypeName function
std::unordered_map<ULONGLONG, std::wstring> typeNameCache;
std::mutex cacheMutex; // Mutex for thread-safe access to the cache (if multithreading is implemented)

int wmain(int argc, wchar_t* argv[]) {
	if (argc < 2) {
		std::wcerr << L"Usage: DumpPDB.exe <path-to-pdb-file> [file-prefix]" << std::endl;
		return 1;
	}

	// Get the file prefix filter from command-line arguments
	std::wstring filePrefix;
	if (argc >= 3) {
		filePrefix = argv[2];
	}

	// Initialize COM library
	HRESULT hr = CoInitialize(NULL);
	if (FAILED(hr)) {
		std::wcerr << L"CoInitialize failed" << std::endl;
		return 1;
	}

	CComPtr<IDiaDataSource> pSource;
	hr = CoCreateInstance(__uuidof(DiaSource), NULL, CLSCTX_INPROC_SERVER,
		__uuidof(IDiaDataSource), (void**)&pSource);
	if (FAILED(hr)) {
		std::wcerr << L"CoCreateInstance failed " << std::hex << hr << std::endl;
		CoUninitialize();
		return 1;
	}

	// Load the PDB file
	hr = pSource->loadDataFromPdb(argv[1]);
	if (FAILED(hr)) {
		std::wcerr << L"loadDataFromPdb failed" << std::endl;
		CoUninitialize();
		return 1;
	}

	CComPtr<IDiaSession> pSession;
	hr = pSource->openSession(&pSession);
	if (FAILED(hr)) {
		std::wcerr << L"openSession failed" << std::endl;
		CoUninitialize();
		return 1;
	}

	CComPtr<IDiaSymbol> pGlobal;
	hr = pSession->get_globalScope(&pGlobal);
	if (FAILED(hr)) {
		std::wcerr << L"get_globalScope failed" << std::endl;
		CoUninitialize();
		return 1;
	}

	// Create JSON root object
	json output;

	// Enumerate symbols
	EnumerateSymbols(pGlobal, output, filePrefix);

	// Output the JSON to a file
	std::ofstream outFile("pdb_dump.json");
	outFile << output.dump(2);
	outFile.close();

	// Reset console title
	SetConsoleTitle(L"DumpPDB - Complete");

	std::wcout << L"PDB information has been dumped to pdb_dump.json" << std::endl;

	CoUninitialize();
	return 0;
}

void EnumerateSymbols(CComPtr<IDiaSymbol> pGlobal, json& output, const std::wstring& filePrefix) {
	HRESULT hr;
	CComPtr<IDiaEnumSymbols> pEnumSymbols;

	// Prepare JSON arrays
	json classesArray = json::array();
	json enumsArray = json::array();
	json globalsArray = json::array();
	json functionsArray = json::array();
	json typedefsArray = json::array();

	// Enumerate all symbols
	hr = pGlobal->findChildren(SymTagNull, NULL, nsNone, &pEnumSymbols);
	if (FAILED(hr)) {
		std::wcerr << L"findChildren failed" << std::endl;
		return;
	}

	LONG totalSymbols = 0;
	pEnumSymbols->get_Count(&totalSymbols);

	CComPtr<IDiaSymbol> pSymbol;
	ULONG celt = 0;
	LONG processedSymbols = 0;
	double lastProgressPercentage = -1.0; // Initialize to -1 to ensure the first update

	while (SUCCEEDED(pEnumSymbols->Next(1, &pSymbol, &celt)) && celt == 1) {
		DWORD symTag = 0;
		pSymbol->get_symTag(&symTag);

		// Update progress
		processedSymbols++;

		double progressPercentage = (static_cast<double>(processedSymbols) * 100.0) / static_cast<double>(totalSymbols);
		// Round to one decimal place
		progressPercentage = floor(progressPercentage * 10.0 + 0.5) / 10.0;

		// Only update the title if the percentage has changed
		if (progressPercentage != lastProgressPercentage) {
			lastProgressPercentage = progressPercentage;
			std::wstringstream titleStream;
			titleStream << L"DumpPDB - Processing (" << std::fixed << std::setprecision(1) << progressPercentage << L"%)";
			SetConsoleTitle(titleStream.str().c_str());
		}

		switch (symTag) {
		case SymTagUDT:
			ProcessUDT(pSymbol, classesArray, filePrefix, totalSymbols, processedSymbols, lastProgressPercentage);
			break;
		case SymTagEnum:
			ProcessEnum(pSymbol, enumsArray, filePrefix, totalSymbols, processedSymbols, lastProgressPercentage);
			break;
		case SymTagFunction:
			ProcessFunction(pSymbol, functionsArray, filePrefix, totalSymbols, processedSymbols, lastProgressPercentage);
			break;
		case SymTagData:
			ProcessData(pSymbol, globalsArray, filePrefix, totalSymbols, processedSymbols, lastProgressPercentage);
			break;
		case SymTagTypedef:
			ProcessTypedef(pSymbol, typedefsArray, totalSymbols, processedSymbols, lastProgressPercentage);
			break;
		default:
			break;
		}

		pSymbol.Release();
	}

	output["Classes"] = classesArray;
	output["Enums"] = enumsArray;
	output["GlobalFunctions"] = functionsArray;
	output["GlobalVariables"] = globalsArray;
	output["Typedefs"] = typedefsArray;
}

void ProcessUDT(CComPtr<IDiaSymbol> pSymbol, json& classesArray, const std::wstring& filePrefix, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage) {
	HRESULT hr;

	json classObject;

	// Get class name
	std::wstring className = GetSymbolName(pSymbol);
	classObject["Name"] = WStringToString(className);

	// Get class size
	ULONGLONG length = 0;
	pSymbol->get_length(&length);
	classObject["Size"] = length;

	// Get class definition file and line number
	std::wstring fileName = GetSymbolFileName(pSymbol);
	if (!fileName.empty()) {
		// Apply file prefix filter
		if (!filePrefix.empty()) {
			if (fileName.find(filePrefix) != 0) {
				// The file name does not start with the prefix, skip this class
				return;
			}
		}
		classObject["SourceFile"] = WStringToString(fileName);
	}

	DWORD lineNumber = GetSymbolLineNumber(pSymbol);
	if (lineNumber != 0)
		classObject["LineNumber"] = lineNumber;

	// Base classes
	json baseClassesArray = json::array();
	CComPtr<IDiaEnumSymbols> pBaseClasses;
	hr = pSymbol->findChildren(SymTagBaseClass, NULL, nsNone, &pBaseClasses);
	if (SUCCEEDED(hr)) {
		CComPtr<IDiaSymbol> pBaseClass;
		ULONG celt = 0;
		while (SUCCEEDED(pBaseClasses->Next(1, &pBaseClass, &celt)) && celt == 1) {
			json baseClassObject;
			std::wstring baseClassName = GetSymbolName(pBaseClass);
			baseClassObject["Name"] = WStringToString(baseClassName);

			// Is virtual base class
			BOOL isVirtual = FALSE;
			pBaseClass->get_virtualBaseClass(&isVirtual);
			baseClassObject["IsVirtual"] = isVirtual ? true : false;

			// Offset
			LONG offset = 0;
			pBaseClass->get_offset(&offset);
			baseClassObject["Offset"] = offset;

			baseClassesArray.push_back(baseClassObject);
			pBaseClass.Release();
		}
	}
	classObject["BaseClasses"] = baseClassesArray;

	// Data members (fields)
	json fieldsArray = json::array();
	CComPtr<IDiaEnumSymbols> pDataMembers;
	hr = pSymbol->findChildren(SymTagData, NULL, nsNone, &pDataMembers);
	if (SUCCEEDED(hr)) {
		CComPtr<IDiaSymbol> pDataMember;
		ULONG celt = 0;
		while (SUCCEEDED(pDataMembers->Next(1, &pDataMember, &celt)) && celt == 1) {
			json fieldObject;
			std::wstring fieldName = GetSymbolName(pDataMember);
			fieldObject["Name"] = WStringToString(fieldName);

			// Type
			CComPtr<IDiaSymbol> pType;
			pDataMember->get_type(&pType);
			std::wstring typeName = GetTypeName(pType);
			fieldObject["Type"] = WStringToString(typeName);

			// Is static
			DWORD locationType = 0;
			pDataMember->get_locationType(&locationType);
			bool isStatic = (locationType == LocIsStatic);
			fieldObject["IsStatic"] = isStatic;

			// Is const
			BOOL isConst = FALSE;
			pDataMember->get_constType(&isConst);
			fieldObject["IsConst"] = isConst ? true : false;

			// Offset
			LONG offset = 0;
			pDataMember->get_offset(&offset);
			fieldObject["Offset"] = offset;

			// Virtual Offset
			uintptr_t virtualAddress = 0;
			pDataMember->get_virtualAddress((ULONGLONG*)&virtualAddress);
			fieldObject["VirtualOffset"] = virtualAddress;

			fieldsArray.push_back(fieldObject);
			pDataMember.Release();
		}
	}
	classObject["Fields"] = fieldsArray;

	// Methods
	json methodsArray = json::array();
	CComPtr<IDiaEnumSymbols> pFunctions;
	hr = pSymbol->findChildren(SymTagFunction, NULL, nsNone, &pFunctions);
	if (SUCCEEDED(hr)) {
		CComPtr<IDiaSymbol> pFunction;
		ULONG celt = 0;
		int virtualMethodIndex = 0;
		while (SUCCEEDED(pFunctions->Next(1, &pFunction, &celt)) && celt == 1) {
			json methodObject;
			std::wstring methodName = GetSymbolName(pFunction);
			methodObject["Name"] = WStringToString(methodName);

			// Is virtual
			BOOL isVirtual = FALSE;
			pFunction->get_virtual(&isVirtual);
			methodObject["IsVirtual"] = isVirtual ? true : false;

			// Is pure virtual
			BOOL isPureVirtual = FALSE;
			pFunction->get_pure(&isPureVirtual);
			methodObject["IsPureVirtual"] = isPureVirtual ? true : false;

			// Is static
			BOOL isStatic = FALSE;
			pFunction->get_isStatic(&isStatic);
			methodObject["IsStatic"] = isStatic ? true : false;

			// Is const
			BOOL isConst = FALSE;
			pFunction->get_constType(&isConst);
			methodObject["IsConst"] = isConst ? true : false;

			// Virtual method index (approximate)
			if (isVirtual)
				methodObject["VirtualMethodIndex"] = virtualMethodIndex++;

			// Virtual Offset
			uintptr_t virtualAddress = 0;
			pFunction->get_virtualAddress((ULONGLONG*)&virtualAddress);
			methodObject["VirtualOffset"] = virtualAddress;

			// Parameters
			json paramsArray = json::array();
			CComPtr<IDiaEnumSymbols> pParams;
			hr = pFunction->findChildren(SymTagFunctionArgType, NULL, nsNone, &pParams);
			if (SUCCEEDED(hr)) {
				CComPtr<IDiaSymbol> pParam;
				ULONG celtParam = 0;
				while (SUCCEEDED(pParams->Next(1, &pParam, &celtParam)) && celtParam == 1) {
					json paramObject;

					// Parameter type
					CComPtr<IDiaSymbol> pType;
					pParam->get_type(&pType);
					std::wstring paramTypeName = GetTypeName(pType);
					paramObject["Type"] = WStringToString(paramTypeName);

					paramsArray.push_back(paramObject);
					pParam.Release();
				}
			}
			methodObject["Parameters"] = paramsArray;

			methodsArray.push_back(methodObject);
			pFunction.Release();
		}
	}
	classObject["Methods"] = methodsArray;

	classesArray.push_back(classObject);
}

void ProcessEnum(CComPtr<IDiaSymbol> pSymbol, json& enumsArray, const std::wstring& filePrefix, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage) {
	json enumObject;

	// Get enum name
	std::wstring enumName = GetSymbolName(pSymbol);
	enumObject["Name"] = WStringToString(enumName);

	// Underlying type
	CComPtr<IDiaSymbol> pType;
	pSymbol->get_type(&pType);
	std::wstring underlyingTypeName = GetTypeName(pType);
	enumObject["UnderlyingType"] = WStringToString(underlyingTypeName);

	// Get enum definition file and line number
	std::wstring fileName = GetSymbolFileName(pSymbol);
	if (!fileName.empty()) {
		// Apply file prefix filter
		if (!filePrefix.empty()) {
			if (fileName.find(filePrefix) != 0) {
				// The file name does not start with the prefix, skip this enum
				return;
			}
		}
		enumObject["SourceFile"] = WStringToString(fileName);
	}

	DWORD lineNumber = GetSymbolLineNumber(pSymbol);
	if (lineNumber != 0)
		enumObject["LineNumber"] = lineNumber;

	// Enum values
	json valuesArray = json::array();
	CComPtr<IDiaEnumSymbols> pEnumValues;
	HRESULT hr = pSymbol->findChildren(SymTagData, NULL, nsNone, &pEnumValues);
	if (SUCCEEDED(hr)) {
		CComPtr<IDiaSymbol> pEnumValue;
		ULONG celt = 0;
		while (SUCCEEDED(pEnumValues->Next(1, &pEnumValue, &celt)) && celt == 1) {
			json valueObject;
			std::wstring valueName = GetSymbolName(pEnumValue);
			valueObject["Name"] = WStringToString(valueName);

			// Value
			VARIANT value;
			VariantInit(&value);
			pEnumValue->get_value(&value);
			if (value.vt == VT_INT) {
				valueObject["Value"] = value.intVal;
			}
			else if (value.vt == VT_UI4) {
				valueObject["Value"] = value.uintVal;
			}
			else if (value.vt == VT_I8) {
				valueObject["Value"] = value.llVal;
			}
			else if (value.vt == VT_UI8) {
				valueObject["Value"] = value.ullVal;
			}
			else {
				valueObject["Value"] = nullptr;
			}
			VariantClear(&value);

			valuesArray.push_back(valueObject);
			pEnumValue.Release();
		}
	}
	enumObject["Values"] = valuesArray;

	enumsArray.push_back(enumObject);
}

void ProcessTypedef(CComPtr<IDiaSymbol> pSymbol, json& typedefsArray, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage) {
	json typedefObject;

	// Get typedef name
	std::wstring typedefName = GetSymbolName(pSymbol);
	typedefObject["Name"] = WStringToString(typedefName);

	// Underlying type
	CComPtr<IDiaSymbol> pType;
	pSymbol->get_type(&pType);
	std::wstring underlyingTypeName = GetTypeName(pType);
	typedefObject["UnderlyingType"] = WStringToString(underlyingTypeName);

	typedefsArray.push_back(typedefObject);
}

void ProcessFunction(CComPtr<IDiaSymbol> pSymbol, json& functionsArray, const std::wstring& filePrefix, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage) {
	json functionObject;

	// Get function name
	std::wstring functionName = GetSymbolName(pSymbol);
	functionObject["Name"] = WStringToString(functionName);

	// Is static
	BOOL isStatic = FALSE;
	pSymbol->get_isStatic(&isStatic);
	functionObject["IsStatic"] = isStatic ? true : false;

	// Is const
	BOOL isConst = FALSE;
	pSymbol->get_constType(&isConst);
	functionObject["IsConst"] = isConst ? true : false;

	// Get function definition file and line number
	std::wstring fileName = GetSymbolFileName(pSymbol);
	if (!fileName.empty()) {
		// Apply file prefix filter
		if (!filePrefix.empty()) {
			if (fileName.find(filePrefix) != 0) {
				// The file name does not start with the prefix, skip this function
				return;
			}
		}
		functionObject["SourceFile"] = WStringToString(fileName);
	}

	DWORD lineNumber = GetSymbolLineNumber(pSymbol);
	if (lineNumber != 0)
		functionObject["LineNumber"] = lineNumber;

	// Virtual Offset
	uintptr_t virtualAddress = 0;
	pSymbol->get_virtualAddress((ULONGLONG*)&virtualAddress);
	functionObject["VirtualOffset"] = virtualAddress;

	// Parameters
	json paramsArray = json::array();
	CComPtr<IDiaEnumSymbols> pParams;
	HRESULT hr = pSymbol->findChildren(SymTagFunctionArgType, NULL, nsNone, &pParams);
	if (SUCCEEDED(hr)) {
		CComPtr<IDiaSymbol> pParam;
		ULONG celtParam = 0;
		while (SUCCEEDED(pParams->Next(1, &pParam, &celtParam)) && celtParam == 1) {
			json paramObject;

			// Parameter type
			CComPtr<IDiaSymbol> pType;
			pParam->get_type(&pType);
			std::wstring paramTypeName = GetTypeName(pType);
			paramObject["Type"] = WStringToString(paramTypeName);

			paramsArray.push_back(paramObject);
			pParam.Release();
		}
	}
	functionObject["Parameters"] = paramsArray;

	functionsArray.push_back(functionObject);
}

void ProcessData(CComPtr<IDiaSymbol> pSymbol, json& globalsArray, const std::wstring& filePrefix, LONG totalSymbols, LONG& processedSymbols, double& lastProgressPercentage) {
	json dataObject;

	// Get variable name
	std::wstring varName = GetSymbolName(pSymbol);
	dataObject["Name"] = WStringToString(varName);

	// Type
	CComPtr<IDiaSymbol> pType;
	pSymbol->get_type(&pType);
	std::wstring typeName = GetTypeName(pType);
	dataObject["Type"] = WStringToString(typeName);

	// Is static
	DWORD locationType = 0;
	pSymbol->get_locationType(&locationType);
	dataObject["IsStatic"] = (locationType == LocIsStatic) ? true : false;

	// Is const
	BOOL isConst = FALSE;
	pSymbol->get_constType(&isConst);
	dataObject["IsConst"] = isConst ? true : false;

	// Get variable definition file and line number
	std::wstring fileName = GetSymbolFileName(pSymbol);
	if (!fileName.empty()) {
		// Apply file prefix filter
		if (!filePrefix.empty()) {
			if (fileName.find(filePrefix) != 0) {
				// The file name does not start with the prefix, skip this variable
				return;
			}
		}
		dataObject["SourceFile"] = WStringToString(fileName);
	}

	DWORD lineNumber = GetSymbolLineNumber(pSymbol);
	if (lineNumber != 0)
		dataObject["LineNumber"] = lineNumber;

	// Virtual Offset
	uintptr_t virtualAddress = 0;
	pSymbol->get_virtualAddress((ULONGLONG*)&virtualAddress);
	dataObject["VirtualOffset"] = virtualAddress;

	globalsArray.push_back(dataObject);
}

// Helper functions

std::wstring GetSymbolName(CComPtr<IDiaSymbol> pSymbol) {
	BSTR bstrName = NULL;
	pSymbol->get_name(&bstrName);
	std::wstring name = bstrName ? bstrName : L"";
	SysFreeString(bstrName);
	return name;
}

std::wstring GetUndecoratedName(CComPtr<IDiaSymbol> pSymbol) {
	BSTR bstrName = NULL;
	pSymbol->get_undecoratedName(&bstrName);
	std::wstring name = bstrName ? bstrName : L"";
	SysFreeString(bstrName);
	return name;
}

std::wstring GetSymbolFileName(CComPtr<IDiaSymbol> pSymbol) {
	// Try to get the source file name directly
	BSTR bstrFileName = NULL;
	HRESULT hr = pSymbol->get_sourceFileName(&bstrFileName);
	if (SUCCEEDED(hr) && bstrFileName) {
		std::wstring fileName = bstrFileName;
		SysFreeString(bstrFileName);
		return fileName;
	}

	// If the above fails, try to get it via the line number
	CComPtr<IDiaLineNumber> pLineNumber;
	hr = pSymbol->getSrcLineOnTypeDefn(&pLineNumber);
	if (SUCCEEDED(hr) && pLineNumber) {
		CComPtr<IDiaSourceFile> pSourceFile;
		hr = pLineNumber->get_sourceFile(&pSourceFile);
		if (SUCCEEDED(hr) && pSourceFile) {
			hr = pSourceFile->get_fileName(&bstrFileName);
			if (SUCCEEDED(hr) && bstrFileName) {
				std::wstring fileName = bstrFileName;
				SysFreeString(bstrFileName);
				return fileName;
			}
		}
	}

	return L"";
}

DWORD GetSymbolLineNumber(CComPtr<IDiaSymbol> pSymbol) {
	CComPtr<IDiaLineNumber> pLineNumber;
	HRESULT hr = pSymbol->getSrcLineOnTypeDefn(&pLineNumber);
	if (SUCCEEDED(hr) && pLineNumber) {
		DWORD lineNumber = 0;
		hr = pLineNumber->get_lineNumber(&lineNumber);
		if (SUCCEEDED(hr)) {
			return lineNumber;
		}
	}
	return 0;
}

std::wstring GetBasicTypeName(DWORD baseType, DWORD length) {
	switch (baseType) {
	case btVoid:
		return L"void";
	case btChar:
		return L"char";
	case btWChar:
		return L"wchar_t";
	case btInt:
		if (length == 1) return L"int8_t";
		else if (length == 2) return L"int16_t";
		else if (length == 4) return L"int32_t";
		else if (length == 8) return L"int64_t";
		else return L"int";
	case btUInt:
		if (length == 1) return L"uint8_t";
		else if (length == 2) return L"uint16_t";
		else if (length == 4) return L"uint32_t";
		else if (length == 8) return L"uint64_t";
		else return L"unsigned int";
	case btFloat:
		if (length == 4) return L"float";
		else if (length == 8) return L"double";
		else if (length == 10) return L"long double";
		else return L"float";
	case btBool:
		return L"bool";
	case btLong:
		return L"long";
	case btULong:
		return L"unsigned long";
	default:
		return L"unknown";
	}
}

std::wstring GetTypeName(CComPtr<IDiaSymbol> pType) {
	if (!pType)
		return L"";

	DWORD typeId = 0;
	pType->get_symIndexId(&typeId);

	{
		// Lock the cache mutex
		std::lock_guard<std::mutex> lock(cacheMutex);

		// Check if the type name is already in the cache
		auto it = typeNameCache.find(typeId);
		if (it != typeNameCache.end()) {
			return it->second;
		}
	}
	

	DWORD symTag = 0;
	pType->get_symTag(&symTag);
	std::wstring typeName;

	if (symTag == SymTagPointerType) {
		CComPtr<IDiaSymbol> pBaseType;
		pType->get_type(&pBaseType);
		std::wstring baseTypeName = GetTypeName(pBaseType);
		typeName = baseTypeName + L"*";
	}
	else if (symTag == SymTagArrayType) {
		CComPtr<IDiaSymbol> pBaseType;
		pType->get_type(&pBaseType);
		DWORD count = 0;
		pType->get_count(&count);
		std::wstring baseTypeName = GetTypeName(pBaseType);
		typeName = baseTypeName + L"[" + std::to_wstring(count) + L"]";
	}
	else if (symTag == SymTagBaseType) {
		DWORD baseType;
		pType->get_baseType(&baseType);
		ULONGLONG length = 0;
		pType->get_length(&length);
		typeName = GetBasicTypeName(baseType, (DWORD)length);
	}
	else {
		// For other types, return the name
		typeName = GetSymbolName(pType);
	}

	// Cache the type name
	{
		// Lock the cache mutex
		std::lock_guard<std::mutex> lock(cacheMutex);
		typeNameCache[typeId] = typeName;
	}
	return typeName;
}
