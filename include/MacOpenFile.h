#ifndef MAC_OPEN_FILE_H
#define MAC_OPEN_FILE_H

#include <string>
#include <vector>

std::vector<std::string> GetStartupOpenFiles();
std::vector<std::string> GetPendingOpenFiles();

#endif // MAC_OPEN_FILE_H
