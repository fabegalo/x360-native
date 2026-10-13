extern "C" __declspec(dllimport) int identity();
int main() { return identity() == IDENTITY ? 0 : 42; }
