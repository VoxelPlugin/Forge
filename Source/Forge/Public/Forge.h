// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "miniz.h"
#include "Commandlets/Commandlet.h"
#include "Forge.generated.h"

struct FForgeLambdaCaller
{
	template<typename T>
	FORCEINLINE auto operator+(T&& Lambda) -> decltype(auto)
	{
		return Lambda();
	}
};

#define INLINE_LAMBDA FForgeLambdaCaller() + [&]()

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

#undef check
#undef verify
#undef UE_LOG

#define FFileHelper ERROR
#define IFileManager ERROR

FORGE_API FString EscapeTeamCity(FString Text);
FORGE_API void Internal_Log(FString Line);
FORGE_API void Internal_LogFatal(const FString& Line);
FORGE_API void Internal_LogSummary(const FString& Line);

#define LOG(Text, ...) Internal_Log(FString::Printf(TEXT(Text), ##__VA_ARGS__))
#define LOG_FATAL(Text, ...) Internal_LogFatal(FString::Printf(TEXT(Text), ##__VA_ARGS__))

#define LOG_SCOPE(Name) \
	LOG("##teamcity[blockOpened name='" Name "']"); \
	ON_SCOPE_EXIT \
	{ \
		LOG("##teamcity[blockClosed name='" Name "']"); \
	}

#define check(...) \
	if (!(__VA_ARGS__)) \
	{ \
		LOG_FATAL("Assert failed: %s %s:%d", *FString(#__VA_ARGS__), *FString(__FILE__), __LINE__); \
	}

using FFunction = void(*)();
extern FORGE_API TMap<FString, FFunction> GForgeNameToFunction;

#define REGISTER_FORGE_COMMAND(Name) \
	void Name(); \
	int32 RegisterForge_ ## Name = [] \
	{ \
		check(!GForgeNameToFunction.Contains(#Name)); \
		GForgeNameToFunction.Add(#Name, Name); \
		return 0; \
	}(); \
	\
	void Name()

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FORGE_API FString SecondsToString(double Value);
FORGE_API FString BytesToString(double Value);
FORGE_API FString NumberToString(double Value);

FORGE_API int32 StringToInt(const FStringView& Text);
FORGE_API int64 StringToInt64(const FStringView& Text);

FORGE_API float StringToFloat(const FStringView& Text);
FORGE_API double StringToDouble(const FStringView& Text);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FORGE_API bool IsWindows();
FORGE_API bool IsMac();
FORGE_API bool IsLinux();

FORGE_API FString GetRootDirectory();

FORGE_API FString GetWorkingDirectory();
FORGE_API void SetWorkingDirectory(const FString& NewWorkingDirectory);

FORGE_API FString Exec(
	const FString& CommandLine,
	const TSet<int32>& ValidExitCodes = { 0 });

FORGE_API FString Exec_PostErrors(
	const FString& CommandLine,
	const TSet<int32>& ValidExitCodes = { 0 });

FORGE_API bool TryExec(
	const FString& CommandLine,
	const TSet<int32>& ValidExitCodes = { 0 });

FORGE_API bool TryExec(
	const FString& CommandLine,
	FString& Output,
	const TSet<int32>& ValidExitCodes = { 0 });

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FORGE_API FString Git_GetRevision();
FORGE_API int32 Git_GetChangelist();
FORGE_API void Git_Fetch();

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FORGE_API bool HasCommandLineSwitch(const FString& Name);
FORGE_API FString GetCommandLineValue(const FString& Name);
FORGE_API bool GetCommandLineBool(const FString& Name);
FORGE_API TArray<FString> GetCommandLineArray(const FString& Name);
FORGE_API TOptional<FString> TryGetCommandLineValue(const FString& Name);

FORGE_API FString GetServerToken();

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct FUnrealVersion
{
	int32 Minor = 0;
	int32 Major = 0;

	int32 GetNumber() const
	{
		return Major * 100 + Minor;
	}
	FString ToString() const
	{
		return FString::Printf(TEXT("%d.%d"), Major, Minor);
	}
	FString ToString_NoDot() const
	{
		return FString::FromInt(GetNumber());
	}
};
FORGE_API FUnrealVersion GetCommandLineUnrealVersion();

FORGE_API void SetupLinuxToolchainFor(const FUnrealVersion& UnrealVersion);

enum class EEngineType
{
	Source,
	Launcher
};

FORGE_API FString GetEnginePath(
	const FUnrealVersion& UnrealVersion,
	EEngineType EngineType);

FORGE_API FString GetRunUATPath(
	const FUnrealVersion& UnrealVersion,
	EEngineType EngineType);

FORGE_API FString RunUAT(
	const FUnrealVersion& UnrealVersion,
	EEngineType EngineType,
	const FString& Command);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

class FORGE_API FHttpGet
{
public:
	explicit FHttpGet(const FString& Url)
		: Url(Url)
	{
	}
	~FHttpGet();

	FHttpGet& Header(const FString& Key, const FString& Value)
	{
		check(!Headers.Contains(Key));
		Headers.Add(Key, Value);
		return *this;
	}
	FHttpGet& QueryParameter(const FString& Key, const FString& Value)
	{
		check(!QueryParameters.Contains(Key));
		QueryParameters.Add(Key, Value);
		return *this;
	}

private:
	const FString Url;
	TMap<FString, FString> Headers;
	TMap<FString, FString> QueryParameters;
};
FORGE_API FHttpGet Http_Get(const FString& Url);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

class FORGE_API FHttpPost
{
public:
	explicit FHttpPost(const FString& Url)
		: Url(Url)
	{
	}
	~FHttpPost();

	FHttpPost& Header(const FString& Key, const FString& Value)
	{
		check(!Headers.Contains(Key));
		Headers.Add(Key, Value);
		return *this;
	}
	FHttpPost& QueryParameter(const FString& Key, const FString& Value)
	{
		check(!QueryParameters.Contains(Key));
		QueryParameters.Add(Key, Value);
		return *this;
	}
	FHttpPost& Content(const FString& Value)
	{
		PrivateContent = Value;
		return *this;
	}

private:
	const FString Url;
	TMap<FString, FString> Headers;
	TMap<FString, FString> QueryParameters;
	FString PrivateContent;
};
FORGE_API FHttpPost Http_Post(const FString& Url);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

struct FSlackAttachment
{
	FString Title;
	FString ImageUrl;
};
FORGE_API void PostSlackMessage(
	const FString& Message,
	const TArray<FSlackAttachment>& Attachments = {});

FORGE_API void PostFatalSlackMessage(
	const FString& Message,
	const TArray<FSlackAttachment>& Attachments = {});

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FORGE_API void CopyDirectory_SkipGit(
	const FString& Source,
	const FString& Dest);

FORGE_API bool DirectoryExists(const FString& Path);
FORGE_API void DeleteDirectory(const FString& Path);
FORGE_API void MakeDirectory(const FString& Path);

FORGE_API bool FileExists(const FString& Path);
FORGE_API void DeleteFile(const FString& Path);

FORGE_API void MoveFile(
	const FString& OldPath,
	const FString& NewPath);

FORGE_API TArray<FString> ListChildren_FileNames(const FString& Path);
FORGE_API TArray<FString> ListChildren_DirectoryNames(const FString& Path);
FORGE_API TArray<FString> ListChildrenRecursive_FilePaths(const FString& Path);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

enum class EZipType
{
	Zip,
	SevenZip
};
FORGE_API void ZipDirectory(
	EZipType ZipType,
	const FString& Directory,
	const FString& Output,
	int32 Compression = -1);

FORGE_API void Unzip(
	const FString& ZipPath,
	const FString& OutputDirectory);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FORGE_API TArray64<uint8> LoadBinaryFile(const FString& Path);

FORGE_API void SaveBinaryFile(
	const FString& Path,
	const TArray64<uint8>& Content);

FORGE_API FString LoadTextFile(const FString& Path);

FORGE_API void SaveTextFile(
	const FString& Path,
	const FString& Content);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FORGE_API FString JsonToString(
	const TSharedRef<FJsonObject>& JsonObject,
	bool bPrettyPrint);

FORGE_API TSharedRef<FJsonObject> StringToJson(const FString& String);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

class FORGE_API FZipWriter
{
public:
	FZipWriter();
	~FZipWriter();

	void Write(
		const FString& Path,
		TConstArrayView64<uint8> Data);

	TArray64<uint8> Finalize();

private:
	mz_zip_archive Archive;
};

FORGE_API TArray64<uint8> ZipDirectory(const FString& Path);

FORGE_API void ExtractZip(
	TConstArrayView64<uint8> Data,
	const FString& Path);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FORGE_API void SetTeamCityParameter(
	const FString& Name,
	const FString& Value);

FORGE_API void RClone_Copy(
	const FString& Source,
	const FString& Target);

FORGE_API bool RClone_FileExists(const FString& Path);

FORGE_API TArray64<uint8> Compress_Oodle(TConstArrayView64<uint8> Data);
FORGE_API TArray64<uint8> Decompress_Oodle(const TArray64<uint8>& CompressedData);

FORGE_API FString ComputeSha1(TConstArrayView64<uint8> Data);

FORGE_API FString GenerateAESKey();

FORGE_API void EncryptData(
	TArrayView64<uint8> Data,
	const FString& AesKey);

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

UCLASS()
class FORGE_API UForgeCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    virtual int32 Main(const FString& Params) override;
};