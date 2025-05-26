#include "SourceMap.h"
#include <fstream>

ecpps::SourceMap::SourceMap(CompilerConfig& config)
	: _config(std::ref(config))
{
	for (const auto& fileName : config.sourceFiles)
	{
		std::ifstream file{ fileName, std::ios::binary | std::ios::ate };
		if (!file)
		{
			// TODO: Error
			return;
		}

		const auto size = file.tellg();
		std::string content(static_cast<std::size_t>(size), '\0');

		file.seekg(0);
		file.read(content.data(), size);

		if (content.starts_with("\xEF\xBB\xBF"))
			content.erase(0, 3);

		SourceFile sourceFile{};
		sourceFile.name = fileName;
		sourceFile.contents = content;
	}
}