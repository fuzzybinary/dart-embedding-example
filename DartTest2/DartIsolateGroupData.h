#pragma once

#include <string>

class DartIsolateGroupData 
{
public:
	DartIsolateGroupData(
		std::string advisoryScriptUri,
		std::string advisoryScriptEntryPoint
	);

private:
	std::string _advisoryScriptUri;
	std::string _advisoryScriptEntryPoint;

};