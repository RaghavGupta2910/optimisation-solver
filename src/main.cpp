#include <iostream>
#include <string>
using namespace std;

int main(int argc, char *argv[]) {

  if (argc == 5 && std::string(argv[1]) == "--solver" &&
      std::string(argv[3]) == "--instances") {
    string solverPath = argv[2];
    string instanceFile = argv[4];
    cout << "Solver: " << solverPath << '\n';
    cout << "Instances: " << instanceFile << '\n';
  }

  return 0;
}