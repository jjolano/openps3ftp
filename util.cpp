#include <string>
#include <cctype>

std::string string_to_upper(std::string str)
{
	for(std::string::iterator c = str.begin(); c != str.end(); c++)
	{
		*c = toupper(*c);
	}

	return str;
}
