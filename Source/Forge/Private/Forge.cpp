// Copyright Voxel Plugin SAS. All Rights Reserved.

#pragma push_macro("UE_LOG")
#include "Forge.h"
#pragma pop_macro("UE_LOG")

#include "HttpModule.h"
#include "HttpManager.h"
#include "PlatformHttp.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Compression/OodleDataCompressionUtil.h"

#undef FFileHelper
#undef IFileManager

IMPLEMENT_MODULE(FDefaultModuleImpl, Forge);

DECLARE_LOG_CATEGORY_EXTERN(LogForge, Log, All);
DEFINE_LOG_CATEGORY(LogForge);

TMap<FString, FFunction> GForgeNameToFunction;
FString GForgeCmd;
FString GForgeArgs;

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int64 IntPow(const int64 Value, const int64 Exponent)
{
	check(Exponent >= 0);
	check(Exponent < 1024);

	int64 Result = 1;
	for (int32 Index = 0; Index < Exponent; Index++)
	{
		Result *= Value;
	}
	return Result;
}

FString SecondsToString(double Value)
{
	if (Value < 1)
	{
		return FString::Printf(TEXT("%lldms"), FMath::CeilToInt(Value * 1000));
	}

	const int64 Hours = FMath::FloorToInt(Value / 3600);
	Value -= Hours * 3600;

	const int64 Minutes = FMath::FloorToInt(Value / 60);
	Value -= Minutes * 60;

	const int64 Seconds = FMath::CeilToInt(Value);

	if (Hours == 0)
	{
		if (Minutes == 0)
		{
			return FString::Printf(TEXT("%llds"), Seconds);
		}

		return FString::Printf(TEXT("%lldm%llds"), Minutes, Seconds);
	}

	return FString::Printf(TEXT("%lldh%lldm%llds"), Hours, Minutes, Seconds);
}

FString BytesToString(double Value)
{
	const TCHAR* Unit = TEXT("");

	FNumberFormattingOptions Options;
	Options.MaximumFractionalDigits = 1;

	if (Value > 1024)
	{
		Options.MinimumFractionalDigits = 1;

		Unit = TEXT("K");
		Value /= 1024;

		if (Value > 1024)
		{
			Unit = TEXT("M");
			Value /= 1024;

			if (Value > 1024)
			{
				Unit = TEXT("G");
				Value /= 1024;

				if (Value > 1024)
				{
					Unit = TEXT("T");
					Value /= 1024;
				}
			}
		}
	}

	return FText::Format(INVTEXT("{0}{1}B"), FText::AsNumber(Value, &Options), FText::FromString(Unit)).ToString();
}

FString NumberToString(double Value)
{
	const TCHAR* Unit = TEXT("");

	FNumberFormattingOptions Options;
	Options.MaximumFractionalDigits = 1;

	if (Value > 1000)
	{
		Options.MinimumFractionalDigits = 1;

		Unit = TEXT("K");
		Value /= 1000;

		if (Value > 1000)
		{
			Unit = TEXT("M");
			Value /= 1000;

			if (Value > 1000)
			{
				Unit = TEXT("G");
				Value /= 1000;

				if (Value > 1000)
				{
					Unit = TEXT("T");
					Value /= 1000;
				}
			}
		}
	}

	return FText::Format(INVTEXT("{0}{1}"), FText::AsNumber(Value, &Options), FText::FromString(Unit)).ToString();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

int32 StringToInt(const FStringView& Text)
{
	const int32 Integer = FCString::Atoi(Text.GetData());

	if (FString::Printf(TEXT("%d"), Integer) != Text)
	{
		LOG_FATAL("Failed to parse integer %s", Text.GetData());
	}

	return Integer;
}

int64 StringToInt64(const FStringView& Text)
{
	const int64 Integer = FCString::Atoi64(Text.GetData());

	if (FString::Printf(TEXT("%lld"), Integer) != Text)
	{
		LOG_FATAL("Failed to parse integer %s", Text.GetData());
	}

	return Integer;
}

bool IsFloat(FStringView Text)
{
	if (Text.Len() == 0)
	{
		return false;
	}

	if (Text[0] == TEXT('-') ||
		Text[0] == TEXT('+'))
	{
		Text.RemovePrefix(1);
	}

	if (Text.Len() == 0)
	{
		return false;
	}

	if (Text[Text.Len() - 1] == TEXT('f'))
	{
		Text.RemoveSuffix(1);
	}

	if (Text.Len() == 0)
	{
		return false;
	}

	while (
		Text.Len() > 0 &&
		FChar::IsDigit(Text[0]))
	{
		Text.RemovePrefix(1);
	}

	if (Text.Len() == 0)
	{
		return true;
	}

	if (Text[0] == TEXT('.'))
	{
		Text.RemovePrefix(1);

		if (Text.Len() == 0)
		{
			return true;
		}

		while (
			Text.Len() > 0 &&
			FChar::IsDigit(Text[0]))
		{
			Text.RemovePrefix(1);
		}

		if (Text.Len() == 0)
		{
			return true;
		}
	}

	if (Text[0] == TEXT('e'))
	{
		Text.RemovePrefix(1);

		const int64 Integer = FCString::Atoi64(Text.GetData());
		return FString::Printf(TEXT("%lld"), Integer) == Text;
	}

	return true;
}

float StringToFloat(const FStringView& Text)
{
	if (!IsFloat(Text))
	{
		LOG_FATAL("Not a float: %s", Text.GetData());
	}
	return FCString::Atof(Text.GetData());
}

double StringToDouble(const FStringView& Text)
{
	if (!IsFloat(Text))
	{
		LOG_FATAL("Not a float: %s", Text.GetData());
	}
	return FCString::Atod(Text.GetData());
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool IsWindows()
{
	return PLATFORM_WINDOWS;
}

bool IsMac()
{
	return PLATFORM_MAC;
}

bool IsLinux()
{
	return PLATFORM_LINUX;
}

FString GetRootDirectory()
{
	if (IsWindows())
	{
		return "C:/BUILD";
	}
	else
	{
		return "/Users/Shared/Forge";
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FString GForgeWorkingDirectory;

FString GetWorkingDirectory()
{
	return GForgeWorkingDirectory;
}

void SetWorkingDirectory(const FString& NewWorkingDirectory)
{
	LOG("SetWorkingDirectory %s", *NewWorkingDirectory);

	const FString AbsoluteWorkingDirectory = FPaths::ConvertRelativePathToFull(NewWorkingDirectory);

	if (!FPaths::DirectoryExists(AbsoluteWorkingDirectory))
	{
		LOG_FATAL("SetWorkingDirectory: %s does not exist", *AbsoluteWorkingDirectory);
	}

	GForgeWorkingDirectory = NewWorkingDirectory;
}

bool ExecImpl(
	const FString& CommandLine,
	const bool bAllowFailure,
	FString& Output,
	const TSet<int32>& ValidExitCodes)
{
	check(!GForgeWorkingDirectory.IsEmpty());
	check(FPaths::DirectoryExists(GForgeWorkingDirectory));

	LOG_SCOPE("%s", *CommandLine);

	const double StartTime = FPlatformTime::Seconds();

	void* PipeRead = nullptr;
	void* PipeWrite = nullptr;
	check(FPlatformProcess::CreatePipe(PipeRead, PipeWrite));

	FProcHandle ProcHandle = INLINE_LAMBDA
	{
		if (IsWindows())
		{
			return FPlatformProcess::CreateProc(
				TEXT("cmd.exe"),
				*("/c \"" + CommandLine + "\""),
				false,
				false,
				false,
				nullptr,
				0,
				*GForgeWorkingDirectory,
				PipeWrite,
				nullptr,
				PipeWrite);
		}
		else
		{
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) / "Script.sh";

			check(FFileHelper::SaveStringToFile(CommandLine, *Path));

			return FPlatformProcess::CreateProc(
				TEXT("/bin/bash"),
				*Path,
				false,
				false,
				false,
				nullptr,
				0,
				*GForgeWorkingDirectory,
				PipeWrite,
				nullptr,
				PipeWrite);
		}
	};
	check(ProcHandle.IsValid());

	const auto FlushReadPipe = [&]
	{
		bool bHasReadAnything = false;
		while (true)
		{
			const FString Text = FPlatformProcess::ReadPipe(PipeRead);
			if (Text.IsEmpty())
			{
				return bHasReadAnything;
			}
			bHasReadAnything = true;

			Output += Text;

			TArray<FString> Lines;
			Text.ParseIntoArrayLines(Lines);

			for (FString& Line : Lines)
			{
				Line.TrimStartAndEndInline();

				if (Line.IsEmpty())
				{
					continue;
				}

				if (Line.StartsWith("Warning: Permanently added 'github.com'") ||
					Line.StartsWith("Your branch is up to date") ||
					Line.StartsWith("Already on") ||
					Line.StartsWith("Already up to date."))
				{
					continue;
				}

				LOG("%s", *Line);
			}
		}
	};

	double LastReadTime = FPlatformTime::Seconds();
	while (FPlatformProcess::IsProcRunning(ProcHandle))
	{
		FPlatformProcess::Sleep(0.01f);

		if (FlushReadPipe())
		{
			LastReadTime = FPlatformTime::Seconds();
		}

		if (FPlatformTime::Seconds() - LastReadTime > 30)
		{
			LastReadTime = FPlatformTime::Seconds();

			LOG("[Waiting for command]");
		}
	}

	int32 ReturnCode = 1;
	check(FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode));

	FlushReadPipe();

	FPlatformProcess::ClosePipe(PipeRead, PipeWrite);

	const double EndTime = FPlatformTime::Seconds();

	if (EndTime - StartTime > 10)
	{
		FString PrettyCommandLine = CommandLine;
		CommandLine.Split(TEXT(" "), &PrettyCommandLine, nullptr);

		PrettyCommandLine.RemoveFromStart("\"");
		PrettyCommandLine.RemoveFromEnd("\"");
		PrettyCommandLine = FPaths::GetBaseFilename(PrettyCommandLine);

		LOG("%s took %s", *PrettyCommandLine, *SecondsToString(EndTime - StartTime));
	}

	if (!ValidExitCodes.Contains(ReturnCode))
	{
		if (bAllowFailure)
		{
			return false;
		}

		LOG_FATAL("%s returned %d\nWorking directory: %s", *CommandLine, ReturnCode, *GForgeWorkingDirectory);
	}

	Output.RemoveFromEnd("\n");
	return true;
}

FString Exec(
	const FString& CommandLine,
	const TSet<int32>& ValidExitCodes)
{
	FString Output;
	check(ExecImpl(
		CommandLine,
		false,
		Output,
		ValidExitCodes));
	return Output;
}

FString Exec_PostErrors(
	const FString& CommandLine,
	const TSet<int32>& ValidExitCodes)
{
	LOG("##teamcity[compilationStarted compiler='Unreal']");

	FString Output;
	const bool bSuccess = ExecImpl(CommandLine, true, Output, ValidExitCodes);

	LOG("##teamcity[compilationFinished compiler='Unreal']");

	if (bSuccess)
	{
		return Output;
	}

	TArray<FString> Lines;
	Output.ParseIntoArrayLines(Lines);

	Lines.RemoveAll([](const FString& Line)
	{
		if (Line.Contains("0 Warning(s)") ||
			Line.Contains("0 Error(s)") ||
			Line.Contains("Failed to create pipeline state with combined hash") ||
			Line.Contains("Failed to create compute PSO with combined hash") ||
			Line.Contains("Failed to create compute pipeline with hash") ||
			Line.Contains("LogRHI: Error: Shader:"))
		{
			return true;
		}

		return
			!Line.Contains("error") &&
			!Line.Contains("warning") &&
			!Line.StartsWith("D:\\Perforce\\");
	});

	for (const FString& Line : Lines)
	{
		LOG("##teamcity[message text='%s' status='ERROR']", *Line.Replace(TEXT("'"), TEXT("|'")));
	}

	Lines.SetNum(FMath::Min(Lines.Num(), 10));

	FString Message = "```\n";
	for (const FString& Line : Lines)
	{
		Message += Line + "\n";
	}
	Message += "```";

	PostFatalSlackMessage(Message);
	return {};
}

bool TryExec(
	const FString& CommandLine,
	const TSet<int32>& ValidExitCodes)
{
	FString Output;
	return ExecImpl(
		CommandLine,
		true,
		Output,
		ValidExitCodes);
}

bool TryExec(
	const FString& CommandLine,
	FString& Output,
	const TSet<int32>& ValidExitCodes)
{
	return ExecImpl(
		CommandLine,
		true,
		Output,
		ValidExitCodes);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FString Git_GetRevision()
{
	const FString Revision = Exec("git rev-parse HEAD");
	const FString ShortRevision = Exec("git rev-parse --short HEAD");
	check(Revision.StartsWith(ShortRevision));

	if (ShortRevision.Len() > 9)
	{
		// We have a collision
		LOG_FATAL("Short revision is too long: %s", *ShortRevision);
	}

	return Revision.Left(9);
}

int32 Git_GetChangelist()
{
	return StringToInt(Exec("git rev-list --count HEAD"));
}

void Git_Fetch()
{
	Exec("git reset --hard");
	Exec("git clean -df");
	Exec("git fetch");
	Exec("git fetch --tags --force");
	TryExec("git pull");
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

bool HasCommandLineSwitch(const FString& Name)
{
	check(!Name.Contains("-"));
	check(!Name.Contains("="));

	return FParse::Param(*GForgeArgs, *Name);
}

FString GetCommandLineValue(const FString& Name)
{
	check(!Name.Contains("-"));
	check(!Name.Contains("="));

	FString Value;
	if (!FParse::Value(*GForgeArgs, *("-" + Name + "="), Value, false) ||
		Value.IsEmpty())
	{
		LOG_FATAL("Missing commandline argument: %s", *Name);
	}
	return Value;
}

bool GetCommandLineBool(const FString& Name)
{
	const FString Value = GetCommandLineValue(Name);

	if (Value != "false" &&
		Value != "true")
	{
		LOG_FATAL("Invalid command line value for %s: %s", *Name, *Value);
	}

	return Value == "true";
}

TArray<FString> GetCommandLineArray(const FString& Name)
{
	const FString Value = GetCommandLineValue(Name);

	TArray<FString> Array;
	Value.ParseIntoArray(Array, TEXT(","));
	check(Array.Num() > 0);

	return Array;
}

TOptional<FString> TryGetCommandLineValue(const FString& Name)
{
	check(!Name.Contains("-"));
	check(!Name.Contains("="));

	FString Value;
	if (!FParse::Value(*GForgeArgs, *("-" + Name + "="), Value, false) ||
		Value.IsEmpty())
	{
		return {};
	}
	return Value;
}

FString GetServerToken()
{
	const FString ServerToken = FPlatformMisc::GetEnvironmentVariable(TEXT("SERVER_TOKEN"));

	if (ServerToken.IsEmpty())
	{
		LOG_FATAL("SERVER_TOKEN is empty");
	}

	return ServerToken;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FUnrealVersion GetCommandLineUnrealVersion()
{
	static const FUnrealVersion StaticUnrealVersion = INLINE_LAMBDA
	{
		const FString UnrealVersionString = GetCommandLineValue("UnrealVersion");

		TArray<FString> MajorMinor;
		UnrealVersionString.ParseIntoArray(MajorMinor, TEXT("."));

		if (MajorMinor.Num() != 2)
		{
			LOG_FATAL("Invalid UnrealVersion %s: should be X.X, eg 5.4", *UnrealVersionString);
		}

		FUnrealVersion UnrealVersion;
		UnrealVersion.Major = StringToInt(MajorMinor[0]);
		UnrealVersion.Minor = StringToInt(MajorMinor[1]);

		LOG("Unreal Version: %s", *UnrealVersion.ToString());

		return UnrealVersion;
	};

	return StaticUnrealVersion;
}

void SetupLinuxToolchainFor(const FUnrealVersion& UnrealVersion)
{
	FString Path;

	if (UnrealVersion.ToString() == "5.4")
	{
		Path = "C:/UnrealToolchains/v22_clang-16.0.6-centos7/";
	}
	else if (UnrealVersion.ToString() == "5.5")
	{
		Path = "C:/UnrealToolchains/v23_clang-18.1.0-rockylinux8/";
	}
	else
	{
		LOG_FATAL("Unsupported version: %s, need to update LINUX_MULTIARCH_ROOT", *UnrealVersion.ToString());
	}

	if (!DirectoryExists(Path))
	{
		LOG_FATAL("Missing Linux toolchain: %s", *Path);
	}

	FPlatformMisc::SetEnvironmentVar(TEXT("LINUX_MULTIARCH_ROOT"), *Path);
}

FString GetEnginePath(
	const FUnrealVersion& UnrealVersion,
	const EEngineType EngineType)
{
	const FString Path = INLINE_LAMBDA
	{
		static const TArray<FString> EngineDirectories =
			IsWindows()
			?
			TArray<FString>
			{
				"C:/BUILD",
				"D:/BUILD",
				"C:/ROOT",
				"D:/ROOT",
				"C:/Program Files/Epic Games/",
				GetRootDirectory()
			}
			:
			TArray<FString>
			{
				"/Users/Shared/",
				"/Users/Shared/Epic Games/",
				FString(FPlatformProcess::UserHomeDir()),
				FString(FPlatformProcess::UserHomeDir()) / "ROOT"
			};

		const TArray<FString> EngineNames =
		{
			"UE_" + UnrealVersion.ToString(),
			"UnrealEngine-" + UnrealVersion.ToString(),
		};

		TArray<FString> Candidates;
		for (const FString& Directory : EngineDirectories)
		{
			for (const FString& EngineName : EngineNames)
			{
				const FString EnginePath = Directory / EngineName;
				Candidates.Add(EnginePath);

				if (EngineType == EEngineType::Source)
				{
					if (DirectoryExists(EnginePath / ".git"))
					{
						return EnginePath;
					}
				}
				else
				{
					check(EngineType == EEngineType::Launcher);

					if (FileExists(EnginePath / "Engine" / "Build" / "Build.version"))
					{
						return EnginePath;
					}
				}
			}
		}

		LOG_FATAL("Failed to find engine for %s. Looked in \n%s",
			*UnrealVersion.ToString(),
			*FString::Join(Candidates, TEXT("\n")));

			return FString();
	};
	check(DirectoryExists(Path));

	if (EngineType == EEngineType::Source)
	{
		static TSet<FString> UpdatedEngines;

		if (!UpdatedEngines.Contains(UnrealVersion.ToString()))
		{
			UpdatedEngines.Add(UnrealVersion.ToString());

			LOG_SCOPE("Update engine");

			const FString WorkingDirectory = GetWorkingDirectory();
			ON_SCOPE_EXIT
			{
				SetWorkingDirectory(WorkingDirectory);
			};

			SetWorkingDirectory(Path);

			Exec("git fetch origin " + UnrealVersion.ToString());
			Exec("git merge FETCH_HEAD");
		}
	}

	return Path;
}

FString GetRunUATPath(
	const FUnrealVersion& UnrealVersion,
	const EEngineType EngineType)
{
	const FString EnginePath = GetEnginePath(UnrealVersion, EngineType);

	if (IsWindows())
	{
		return EnginePath / "Engine/Build/BatchFiles/RunUAT.bat";
	}
	else
	{
		return EnginePath / "Engine/Build/BatchFiles/RunUAT.sh";
	}
}

FString RunUAT(
	const FUnrealVersion& UnrealVersion,
	const EEngineType EngineType,
	const FString& Command)
{
	return Exec_PostErrors(FString::Printf(
		TEXT("\"%s\" %s"),
		*GetRunUATPath(UnrealVersion, EngineType),
		*Command));
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FHttpGet::~FHttpGet()
{
	LOG("GET %s", *Url);

	if (!Headers.IsEmpty())
	{
		LOG("\tHeaders:");

		for (const auto& It : Headers)
		{
			LOG("\t\t%s: %s", *It.Key, *It.Value);
		}
	}

	if (!QueryParameters.IsEmpty())
	{
		LOG("\tQuery parameters:");

		for (const auto& It : QueryParameters)
		{
			LOG("\t\t%s: %s", *It.Key, *It.Value);
		}
	}

	const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb("GET");

	for (const auto& It : Headers)
	{
		Request->SetHeader(It.Key, It.Value);
	}

	FString FinalUrl = Url;
	if (QueryParameters.Num() > 0)
	{
		FinalUrl += "?";

		for (const auto& It : QueryParameters)
		{
			if (!FinalUrl.EndsWith("?"))
			{
				FinalUrl += "&";
			}

			if (It.Key != FPlatformHttp::UrlEncode(It.Key))
			{
				LOG_FATAL("Invalid query parameter key: %s", *It.Key);
			}

			FinalUrl += It.Key + "=" + FPlatformHttp::UrlEncode(It.Value);
		}
	}

	Request->SetURL(FinalUrl);

	Request->ProcessRequest();

	while (Request->GetStatus() == EHttpRequestStatus::Processing)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.f);
	}

	const TSharedPtr<IHttpResponse> Response = Request->GetResponse();
	if (!Response)
	{
		LOG_FATAL("GET failed: Failed to connect");
	}

	if (Response->GetResponseCode() != 200)
	{
		LOG_FATAL("GET failed: %d\n%s",
			Response->GetResponseCode(),
			*Response->GetContentAsString());
	}

	check(Request->GetStatus() == EHttpRequestStatus::Succeeded);

	LOG("RESPONSE: %d\n%s",
		Response->GetResponseCode(),
		*Response->GetContentAsString());
}

FHttpGet Http_Get(const FString& Url)
{
	return FHttpGet(Url);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FHttpPost::~FHttpPost()
{
	LOG("POST %s", *Url);

	if (!Headers.IsEmpty())
	{
		LOG("\tHeaders:");

		for (const auto& It : Headers)
		{
			LOG("\t\t%s: %s", *It.Key, *It.Value);
		}
	}

	if (!QueryParameters.IsEmpty())
	{
		LOG("\tQuery parameters:");

		for (const auto& It : QueryParameters)
		{
			LOG("\t\t%s: %s", *It.Key, *It.Value);
		}
	}

	if (!PrivateContent.IsEmpty())
	{
		LOG("%s", *PrivateContent);
	}

	const TSharedRef<IHttpRequest> Request = FHttpModule::Get().CreateRequest();
	Request->SetVerb("POST");

	for (const auto& It : Headers)
	{
		Request->SetHeader(It.Key, It.Value);
	}

	FString FinalUrl = Url;
	if (QueryParameters.Num() > 0)
	{
		FinalUrl += "?";

		for (const auto& It : QueryParameters)
		{
			if (!FinalUrl.EndsWith("?"))
			{
				FinalUrl += "&";
			}

			if (It.Key != FPlatformHttp::UrlEncode(It.Key))
			{
				LOG_FATAL("Invalid query parameter key: %s", *It.Key);
			}

			FinalUrl += It.Key + "=" + FPlatformHttp::UrlEncode(It.Value);
		}
	}

	Request->SetURL(FinalUrl);

	if (!PrivateContent.IsEmpty())
	{
		Request->SetContentAsString(PrivateContent);
	}

	Request->ProcessRequest();

	while (Request->GetStatus() == EHttpRequestStatus::Processing)
	{
		FHttpModule::Get().GetHttpManager().Tick(0.f);
	}

	const TSharedPtr<IHttpResponse> Response = Request->GetResponse();
	if (!Response)
	{
		LOG_FATAL("POST failed: Failed to connect");
	}

	if (Response->GetResponseCode() != 200 &&
		Response->GetResponseCode() != 201)
	{
		LOG_FATAL("POST failed: %d\n%s",
			Response->GetResponseCode(),
			*Response->GetContentAsString());
	}

	check(Request->GetStatus() == EHttpRequestStatus::Succeeded);

	LOG("RESPONSE: %d\n%s",
		Response->GetResponseCode(),
		*Response->GetContentAsString());
}

FHttpPost Http_Post(const FString& Url)
{
	return FHttpPost(Url);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FString GForgeSlackBuildOpsUrl;
bool GForgeIsSendingSlackMessage;

void PostSlackMessage(
	const FString& Message,
	const TArray<FSlackAttachment>& Attachments)
{
	check(!GForgeIsSendingSlackMessage);
	GForgeIsSendingSlackMessage = true;
	ON_SCOPE_EXIT
	{
		check(GForgeIsSendingSlackMessage);
		GForgeIsSendingSlackMessage = false;
	};

	LOG_SCOPE("PostSlackMessage: %s", *Message);

	const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
	Json->SetStringField("text", Message);

	if (Attachments.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> AttachmentsArray;
		for (const FSlackAttachment& Attachment : Attachments)
		{
			const TSharedRef<FJsonObject> AttachmentJson = MakeShared<FJsonObject>();
			AttachmentJson->SetStringField("title", Attachment.Title);
			AttachmentJson->SetStringField("image_url", Attachment.ImageUrl);
			AttachmentsArray.Add(MakeShared<FJsonValueObject>(AttachmentJson));
		}
		Json->SetArrayField("attachments", AttachmentsArray);
	}

	Http_Post(GForgeSlackBuildOpsUrl)
	.Header("Content-type", "application/json")
	.Content(JsonToString(Json, true));
}

void PostFatalSlackMessage(
	const FString& Message,
	const TArray<FSlackAttachment>& Attachments)
{
	FString NewMessage = "*" + GForgeCmd + " failed*\n";
	NewMessage += FPlatformMisc::GetEnvironmentVariable(TEXT("BUILD_URL")) + "\n";
	NewMessage += Message;

	PostSlackMessage(NewMessage, Attachments);

	UE_LOG(LogForge, Fatal, TEXT("%s"), *Message);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void CheckIsValidPath(const FString& Path)
{
	if (IsWindows())
	{
		if (!Path.StartsWith("C:") &&
			!Path.StartsWith("D:") &&
			!Path.StartsWith("Z:"))
		{
			LOG_FATAL("Invalid path: %s", *Path);
		}
	}
	else
	{
		if (!Path.StartsWith("/"))
		{
			LOG_FATAL("Invalid path: %s", *Path);
		}
	}
}

void CopyDirectory_SkipGit(
	const FString& Source,
	const FString& Dest)
{
	CheckIsValidPath(Source);
	CheckIsValidPath(Dest);

	LOG("CopyDirectory_SkipGit %s -> %s", *Source, *Dest);

	check(FPaths::DirectoryExists(Source));

	class FVisitor : public IPlatformFile::FDirectoryVisitor
	{
	public:
		const FString Source;
		const FString Dest;

		FVisitor(
			const FString& Source,
			const FString& Dest)
			: Source(Source)
			, Dest(Dest)
		{
		}

		virtual bool Visit(const TCHAR* FilenameOrDirectory, const bool bIsDirectory) override
		{
			if (bIsDirectory)
			{
				if (!FString(FilenameOrDirectory).Contains(".git"))
				{
					IFileManager::Get().IterateDirectory(FilenameOrDirectory, *this);
				}

				return true;
			}

			FString Path = FilenameOrDirectory;
			check(Path.RemoveFromStart(Source));
			Path = Dest / Path;

			check(IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true));
			check(IFileManager::Get().Copy(*Path, FilenameOrDirectory) == COPY_OK);

			return true;
		}
	};
	FVisitor Visitor(Source, Dest);

	IFileManager::Get().IterateDirectory(*Source, Visitor);
}

bool DirectoryExists(const FString& Path)
{
	CheckIsValidPath(Path);
	return IFileManager::Get().DirectoryExists(*Path);
}

void DeleteDirectory(const FString& Path)
{
	CheckIsValidPath(Path);

	LOG("DeleteDirectory %s", *Path);

	if (FileExists(Path))
	{
		LOG_FATAL("Failed to delete %s: is a file, not a directory", *Path);
	}

	if (!IFileManager::Get().DeleteDirectory(*Path, false, true))
	{
		LOG_FATAL("Failed to delete %s", *Path);
	}
}

void MakeDirectory(const FString& Path)
{
	CheckIsValidPath(Path);

	LOG("MakeDirectory %s", *Path);

	if (DirectoryExists(Path))
	{
		return;
	}

	if (!IFileManager::Get().MakeDirectory(*Path, true))
	{
		LOG_FATAL("Failed to create %s", *Path);
	}
}

bool FileExists(const FString& Path)
{
	CheckIsValidPath(Path);
	return IFileManager::Get().FileExists(*Path);
}

void DeleteFile(const FString& Path)
{
	CheckIsValidPath(Path);

	LOG("DeleteFile %s", *Path);

	if (DirectoryExists(Path))
	{
		LOG_FATAL("Failed to delete %s: is a directory, not a file", *Path);
	}

	if (!IFileManager::Get().Delete(*Path, false, true))
	{
		LOG_FATAL("Failed to delete %s", *Path);
	}
}

void MoveFile(
	const FString& OldPath,
	const FString& NewPath)
{
	CheckIsValidPath(OldPath);
	CheckIsValidPath(NewPath);

	LOG("MoveFile %s -> %s", *OldPath, *NewPath);

	if (!FileExists(OldPath))
	{
		LOG_FATAL("MoveFile: %s does not exist", *OldPath);
	}

	if (FileExists(NewPath) ||
		DirectoryExists(NewPath))
	{
		LOG_FATAL("MoveFile: %s already exists", *NewPath);
	}

	check(IFileManager::Get().Move(*NewPath, *OldPath));
}

TArray<FString> ListChildren_FileNames(const FString& Path)
{
	if (!DirectoryExists(Path))
	{
		LOG_FATAL("ListChildren_FileNames %s: Path does not exist", *Path);
	}

	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *(Path / "*"), true, false);
	return Files;
}

TArray<FString> ListChildren_DirectoryNames(const FString& Path)
{
	if (!DirectoryExists(Path))
	{
		LOG_FATAL("ListChildren_DirectoryNames %s: Path does not exist", *Path);
	}

	TArray<FString> Directories;
	IFileManager::Get().FindFiles(Directories, *(Path / "*"), false, true);
	return Directories;
}

TArray<FString> ListChildrenRecursive_FilePaths(const FString& Path)
{
	if (!DirectoryExists(Path))
	{
		LOG_FATAL("ListChildrenRecursive_FilePaths %s: Path does not exist", *Path);
	}

	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(
		Files,
		*Path,
		TEXT("*"),
		true,
		false);

	return Files;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FString GetSevenZipPath()
{
	const FString BasePath = IPluginManager::Get().FindPlugin("Forge")->GetBaseDir() / "Source" / "ThirdParty";
	check(DirectoryExists(BasePath));

	const FString ExePath = INLINE_LAMBDA
	{
		if (IsWindows())
		{
			return BasePath / "Win64" / "7za.exe";
		}
		else if (IsMac())
		{
			return BasePath / "Mac" / "7zz";
		}
		else
		{
			check(IsLinux());
			return BasePath / "Linux" / "7zz";
		}
	};
	check(FileExists(ExePath));

	return ExePath;
}

void ZipDirectory(
	const EZipType ZipType,
	const FString& Directory,
	const FString& Output,
	const int32 Compression)
{
	LOG_SCOPE("Zipping %s into %s", *Directory, *Output);

	if (!DirectoryExists(Directory))
	{
		LOG_FATAL("ZipDirectory %s: Path does not exist", *Directory);
	}

	if (FileExists(Output) ||
		DirectoryExists(Output))
	{
		LOG_FATAL("ZipDirectory %s: Output path already exists", *Output);
	}

	const FString Format = INLINE_LAMBDA
	{
		if (ZipType == EZipType::Zip)
		{
			return "zip";
		}
		else
		{
			check(ZipType == EZipType::SevenZip);
			return "7z";
		}
	};

	FString Command = GetSevenZipPath() + " a -t" + Format + " \"" + Output + "\" \"" + Directory + "\"";
	if (Compression != -1)
	{
		check(0 <= Compression && Compression <= 9);
		Command += FString::Printf(TEXT(" -mx%d"), Compression);
	}

	Exec(Command);
}

void Unzip(
	const FString& ZipPath,
	const FString& OutputDirectory)
{
	LOG_SCOPE("Unzipping %s", *ZipPath);

	if (!FileExists(ZipPath))
	{
		LOG_FATAL("Unzip: zip %s does not exist", *ZipPath);
	}
	if (!DirectoryExists(OutputDirectory))
	{
		LOG_FATAL("Unzip: output directory %s does not exist", *OutputDirectory);
	}

	Exec(GetSevenZipPath() + " x -y " + "\"" + ZipPath + "\" -o\"" + OutputDirectory + "\"");
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

TArray64<uint8> LoadBinaryFile(const FString& Path)
{
	if (!FileExists(Path))
	{
		LOG_FATAL("LoadBinaryFile %s: Path does not exist", *Path);
	}

	TArray64<uint8> Value;
	if (!FFileHelper::LoadFileToArray(Value, *Path))
	{
		LOG_FATAL("LoadBinaryFile %s: Failed to load", *Path);
	}

	return Value;
}

void SaveBinaryFile(
	const FString& Path,
	const TArray64<uint8>& Content)
{
	LOG("SaveBinaryFile %s %s", *Path, *BytesToString(Content.Num()));

	if (!FFileHelper::SaveArrayToFile(Content, *Path))
	{
		LOG_FATAL("SaveBinaryFile %s: failed to save", *Path);
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FString LoadTextFile(const FString& Path)
{
	if (!FileExists(Path))
	{
		LOG_FATAL("LoadTextFile %s: Path does not exist", *Path);
	}

	FString Value;
	if (!FFileHelper::LoadFileToString(Value, *Path))
	{
		LOG_FATAL("LoadTextFile %s: Failed to load", *Path);
	}

	return Value;
}

void SaveTextFile(
	const FString& Path,
	const FString& Content)
{
	LOG("SaveTextFile %s", *Path);

	if (!FFileHelper::SaveStringToFile(Content, *Path))
	{
		LOG_FATAL("SaveTextFile %s: failed to save", *Path);
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FString JsonToString(
	const TSharedRef<FJsonObject>& JsonObject,
	const bool bPrettyPrint)
{
	FString Result;

	if (bPrettyPrint)
	{
		const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&Result);
		check(FJsonSerializer::Serialize(JsonObject, JsonWriter));
	}
	else
	{
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> JsonWriter = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Result);
		check(FJsonSerializer::Serialize(JsonObject, JsonWriter));
	}

	return Result;
}

TSharedRef<FJsonObject> StringToJson(const FString& String)
{
	TSharedPtr<FJsonObject> JsonObject;
	check(FJsonSerializer::Deserialize(
		TJsonReaderFactory<>::Create(String),
		JsonObject));

	check(JsonObject);
	return JsonObject.ToSharedRef();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void ExtractZip(
	const TConstArrayView64<uint8> Data,
	const FString& Path)
{
	LOG_SCOPE("ExtractZip %s", *Path);

	mz_zip_archive Archive;
	FMemory::Memzero(Archive);

	Archive.m_pIO_opaque = const_cast<void*>(static_cast<const void*>(&Data));
	Archive.m_pRead = [](void* Opaque, const mz_uint64 Offset, void* Buffer, const size_t Size) -> size_t
	{
		const TConstArrayView64<uint8> LocalData = *static_cast<TConstArrayView64<uint8>*>(Opaque);
		check(int64(Offset + Size) <= LocalData.Num());
		FMemory::Memcpy(Buffer, &LocalData[Offset], Size);
		return Size;
	};

	check(mz_zip_reader_init(&Archive, Data.Num(), 0));

	for (int32 Index = 0; Index < int32(Archive.m_total_files); Index++)
	{
		const uint32 Size = mz_zip_reader_get_filename(
			&Archive,
			Index,
			nullptr,
			0);

		check(Size);

		TArray<char> UTF8String;
		UTF8String.SetNumZeroed(Size);

		check(mz_zip_reader_get_filename(
			&Archive,
			Index,
			UTF8String.GetData(),
			UTF8String.Num()) == UTF8String.Num());

		FString FilePath(UTF8String);
		FilePath.TrimToNullTerminator();

		FilePath = Path / FilePath;

		mz_zip_archive_file_stat FileStat;
		check(mz_zip_reader_file_stat(
			&Archive,
			Index,
			&FileStat));

		TArray64<uint8> FileData;
		FileData.SetNumUninitialized(FileStat.m_uncomp_size);

		check(mz_zip_reader_extract_to_mem_no_alloc(
			&Archive,
			Index,
			FileData.GetData(),
			FileData.Num(),
			0,
			nullptr,
			0));

		SaveBinaryFile(FilePath, FileData);
	}

	check(mz_zip_end(&Archive));
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

FZipWriter::FZipWriter()
{
	FMemory::Memzero(Archive);

	check(mz_zip_writer_init_heap(&Archive, 0, 0));
}

FZipWriter::~FZipWriter()
{
	check(mz_zip_end(&Archive));
}

void FZipWriter::Write(
	const FString& Path,
	const TConstArrayView64<uint8> Data)
{
	check(mz_zip_writer_add_mem(
		&Archive,
		TCHAR_TO_UTF8(*Path),
		Data.GetData(),
		Data.Num(),
		MZ_NO_COMPRESSION));
}

TArray64<uint8> FZipWriter::Finalize()
{
	void* Buffer = nullptr;
	size_t BufferSize = 0;
	check(mz_zip_writer_finalize_heap_archive(&Archive, &Buffer, &BufferSize));

	TArray64<uint8> Data = TArray64<uint8>(static_cast<const uint8*>(Buffer), BufferSize);
	mz_free(Buffer);

	return Data;
}

TArray64<uint8> ZipDirectory(const FString& Path)
{
	LOG_SCOPE("ZipDirectory %s", *Path);

	const double StartTime = FPlatformTime::Seconds();

	if (!DirectoryExists(Path))
	{
		LOG_FATAL("ZipDirectory: %s does not exist", *Path);
	}

	const TArray<FString> Files = ListChildrenRecursive_FilePaths(Path);
	check(Files.Num() > 0);

	FZipWriter ZipWriter;

	for (const FString& File : Files)
	{
		FString RelativePath = File;
		check(RelativePath.RemoveFromStart(Path));
		check(RelativePath.RemoveFromStart("/"));

		const TArray64<uint8> Buffer = LoadBinaryFile(File);

		ZipWriter.Write(RelativePath, Buffer);

		LOG("%s: %s", *RelativePath, *BytesToString(Buffer.Num()));
	}

	TArray64<uint8> Data = ZipWriter.Finalize();

	const double EndTime = FPlatformTime::Seconds();

	LOG("Zipping took %s", *SecondsToString(EndTime - StartTime));

	return Data;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

void SetGitHubOutput(
	const FString& Key,
	const FString& Value)
{
	LOG("SetGitHubOutput: %s=%s", *Key, *Value);
	check(!Key.Contains("="));

	const FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("GITHUB_OUTPUT"));
	if (Path.IsEmpty())
	{
		return;
	}
	check(FileExists(Path));

	const FString String = Key + "=" + Value + "\n";
	const FTCHARToUTF8 UnicodeString(*String, String.Len());

	IFileHandle* Handle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*Path, true, true);
	check(Handle);

	Handle->Write(
		reinterpret_cast<const uint8*>(UnicodeString.Get()),
		UnicodeString.Length());

	delete Handle;
}

void RClone_Copy(
	const FString& Source,
	const FString& Target)
{
	const double StartTime = FPlatformTime::Seconds();

	Exec("rclone copyto " + Source + " " + Target);

	const double EndTime = FPlatformTime::Seconds();

	const FString Path = INLINE_LAMBDA
	{
		if (Source.Contains(":") &&
			!Source.Contains(":/"))
		{
			return Target;
		}

		return Source;
	};
	if (!FileExists(Path))
	{
		LOG_FATAL("Invalid rclone: %s does not exist", *Path);
	}

	const int64 FileSize = IFileManager::Get().FileSize(*Path);

	LOG("rclone copy took %fs (%s/s)",
		EndTime - StartTime,
		*BytesToString(FileSize / (EndTime - StartTime)));
}

bool RClone_FileExists(const FString& Path)
{
	LOG_SCOPE("RClone_FileExists %s", *Path);

	FString Output;
	if (!TryExec("rclone lsf " + Path, Output))
	{
		return false;
	}

	if (FPaths::GetCleanFilename(Path) != Output.TrimStartAndEnd())
	{
		LOG("%s != %s", *FPaths::GetCleanFilename(Path), *Output.TrimStartAndEnd());
		return false;
	}

	return true;
}

TArray64<uint8> Compress_Oodle(const TConstArrayView64<uint8> Data)
{
	const double StartTime = FPlatformTime::Seconds();

	TArray64<uint8> CompressedData;
	check(FOodleCompressedArray::CompressData64(
		CompressedData,
		Data.GetData(),
		Data.Num(),
		FOodleDataCompression::ECompressor::Leviathan,
		FOodleDataCompression::ECompressionLevel::Normal));

	const double EndTime = FPlatformTime::Seconds();

	LOG("Compression took %fs (%s/s), %s -> %s",
		EndTime - StartTime,
		*BytesToString(Data.Num() / (EndTime - StartTime)),
		*BytesToString(Data.Num()),
		*BytesToString(CompressedData.Num()));

	// In case we want to encrypt it later
	// Decompressed data will be the original length
	CompressedData.SetNumZeroed(FMath::DivideAndRoundUp<int64>(CompressedData.Num(), 16) * 16);

	return CompressedData;
}

TArray64<uint8> Decompress_Oodle(const TArray64<uint8>& CompressedData)
{
	const double StartTime = FPlatformTime::Seconds();

	TArray64<uint8> Data;
	check(FOodleCompressedArray::DecompressToTArray64(
		Data,
		CompressedData));

	const double EndTime = FPlatformTime::Seconds();

	LOG("Decompression took %fs (%s/s), %s -> %s",
		EndTime - StartTime,
		*BytesToString(Data.Num() / (EndTime - StartTime)),
		*BytesToString(CompressedData.Num()),
		*BytesToString(Data.Num()));

	return Data;
}

FString ComputeSha1(const TConstArrayView64<uint8> Data)
{
	return FSHA1::HashBuffer(Data.GetData(), Data.Num()).ToString();
}

FString GenerateAESKey()
{
	FAES::FAESKey Key;
	for (int32 Index = 0; Index < 32; Index++)
	{
		Key.Key[Index] = uint8(FMath::Rand() & 255);
	}
	return FBase64::Encode(Key.Key, 32);
}

void EncryptData(
	const TArrayView64<uint8> Data,
	const FString& AesKey)
{
	check(Data.Num() % 16 == 0);

	TArray<uint8> AesKeyBits;
	check(FBase64::Decode(AesKey, AesKeyBits));
	check(AesKeyBits.Num() == 32);

	FAES::FAESKey Key;
	FMemory::Memcpy(Key.Key, AesKeyBits.GetData(), 32);

	FAES::EncryptData(Data.GetData(), Data.Num(), Key);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

double GForgeStartTime = 0;
IFileHandle* GForgeLogHandle = nullptr;
FCriticalSection GForgeCriticalSection;

int32 UForgeCommandlet::Main(const FString& Params)
{
	GForgeStartTime = FPlatformTime::Seconds();

	{
		const FString ForgeLogPath = FPaths::ProjectSavedDir() / "ForgeLog.txt";

		GForgeLogHandle = FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*ForgeLogPath, true, true);

		if (!GForgeLogHandle)
		{
			LOG_FATAL("Failed to open %s for write", *ForgeLogPath);
		}
	}
	ON_SCOPE_EXIT
	{
		FScopeLock Lock(&GForgeCriticalSection);

		delete GForgeLogHandle;
		GForgeLogHandle = nullptr;
	};

	class FOutputDeviceForge : public FOutputDevice
	{
	public:
		FCriticalSection CriticalSection;
		TArray<FString> Warnings;
		TArray<FString> Errors;

		virtual bool CanBeUsedOnAnyThread() const override
		{
			return true;
		}
		virtual bool CanBeUsedOnPanicThread() const override
		{
			return true;
		}
		virtual bool CanBeUsedOnMultipleThreads() const override
		{
			return true;
		}
		virtual void Serialize(const TCHAR* Data, const ELogVerbosity::Type Verbosity, const FName& Category) override
		{
			if (Category == LogForge.GetCategoryName())
			{
				return;
			}

			if (Verbosity == ELogVerbosity::Warning)
			{
				LOG("##teamcity[message text='%s %s' status='WARNING']", *Category.ToString(), *FString(Data).Replace(TEXT("'"), TEXT("|'")));

				FScopeLock Lock(&CriticalSection);
				Warnings.Add(FString::Printf(TEXT("%s: %s"), *Category.ToString(), Data));
			}
			else if (Verbosity == ELogVerbosity::Error)
			{
				LOG("##teamcity[message text='%s %s' status='ERROR']", *Category.ToString(), *FString(Data).Replace(TEXT("'"), TEXT("|'")));

				FScopeLock Lock(&CriticalSection);
				Errors.Add(FString::Printf(TEXT("%s: %s"), *Category.ToString(), Data));
			}
			else
			{
				LOG("%s: %s", *Category.ToString(), Data);
			}
		}
	};
	FOutputDeviceForge* OutputDevice = new FOutputDeviceForge();
	GLog->AddOutputDevice(OutputDevice);
	ON_SCOPE_EXIT
	{
		GLog->RemoveOutputDevice(OutputDevice);
	};

	GForgeSlackBuildOpsUrl = FPlatformMisc::GetEnvironmentVariable(TEXT("SLACK_BUILD_OPS_URL"));

	if (GForgeSlackBuildOpsUrl.IsEmpty())
	{
		LOG_FATAL("Missing SLACK_BUILD_OPS_URL");
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("-ForgeCmd="), GForgeCmd) ||
		GForgeCmd.IsEmpty())
	{
		LOG_FATAL("Missing -ForgeCmd=");
	}

	if (!FParse::Value(FCommandLine::Get(), TEXT("-ForgeArgs="), GForgeArgs) ||
		GForgeArgs.IsEmpty())
	{
		LOG_FATAL("Missing -ForgeArgs=");
	}

	const FFunction Function = GForgeNameToFunction.FindRef(GForgeCmd);

	if (!Function)
	{
		LOG_FATAL("No function named %s", *GForgeCmd);
	}

	LOG("Running %s %s", *GForgeCmd, *GForgeArgs);

	Function();

	if (OutputDevice->Warnings.Num() > 0 ||
		OutputDevice->Errors.Num() > 0)
	{
		FString Message;

		if (OutputDevice->Warnings.Num() > 0)
		{
			LOG("%d warnings", OutputDevice->Warnings.Num());

			Message += FString::FromInt(OutputDevice->Warnings.Num()) + " warnings\n";
			Message += "```\n";

			for (const FString& Warning : OutputDevice->Warnings)
			{
				Message += Warning + "\n";
				LOG("%s", *Warning);
			}

			Message += "```\n";
		}

		if (OutputDevice->Errors.Num() > 0)
		{
			LOG("%d errors", OutputDevice->Errors.Num());

			Message += FString::FromInt(OutputDevice->Errors.Num()) + " errors\n";
			Message += "```\n";

			for (const FString& Error : OutputDevice->Errors)
			{
				Message += Error + "\n";
				LOG("%s", *Error);
			}

			Message += "```\n";
		}

		LOG_FATAL("%s", *Message);
	}

	return 0;
}

int32 GForgeBlockIndex = 0;

void Internal_Log(FString Line)
{
	if (!Line.Contains("LogForge"))
	{
		UE_LOG(LogForge, Display, TEXT("%s"), *Line);
	}

	Line.ReplaceInline(TEXT("\n"), TEXT("|n"));
	Line.ReplaceInline(TEXT("\r"), TEXT("|r"));

	const FTCHARToUTF8 UnicodeString(*Line, Line.Len());

	FScopeLock Lock(&GForgeCriticalSection);
	if (!GForgeLogHandle)
	{
		UE_LOG(LogForge, Fatal, TEXT("Cannot log: GForgeLogHandle is null"));
	}

	GForgeLogHandle->Write(
		reinterpret_cast<const uint8*>(UnicodeString.Get()),
		UnicodeString.Length());
}

void Internal_LogFatal(const FString& Line)
{
	UE_DEBUG_BREAK();

	Internal_Log("FATAL: " + Line);

	if (!GForgeIsSendingSlackMessage)
	{
		PostFatalSlackMessage(Line);
	}

	UE_LOG(LogForge, Fatal, TEXT("%s"), *Line);
}