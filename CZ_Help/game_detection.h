#pragma once
#include <windows.h>
#include <string>
#include <vector>

namespace cz {
struct RunningGame { DWORD processId{}; std::wstring path; };
struct GameDiscovery { std::vector<RunningGame> games; std::wstring warning; bool complete{true}; };
enum class GamePathSource { Argument, RunningGame, Configuration, Ambiguous };
struct GamePathChoice { std::wstring path; GamePathSource source{GamePathSource::Configuration}; std::wstring warning; };
bool IsConditionZeroCommandLine(const std::wstring& command);
bool IsGameDirectory(const std::wstring& path);
GameDiscovery DiscoverRunningGames();
GamePathChoice SelectGamePath(const std::wstring& argument,const std::wstring& configured,const GameDiscovery& discovered);
}
