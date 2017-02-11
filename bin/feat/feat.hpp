/* feat.hpp: FTP command helper functions. */

#include "command.hpp"

namespace feat
{
	namespace base
	{
		FTP::Command get_commands(void);
	};

	namespace app
	{
		FTP::Command get_commands(void);
	};

	namespace ext
	{
		FTP::Command get_commands(void);
	};

	FTP::Command get_commands(void);
};
