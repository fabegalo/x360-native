// Explicit, bounded diagnostic cycle for existing AOT outputs. No preparation
// receipts are created or bypassed in the production supervisor. Input and
// captures are supplied by a separate PID-scoped script. Uses SessionWindow
// and the same original handoff format as the production session.
#include "session_window.h"
#include "sha256.h"
#include "../../titles/common/launch_request.h"
#include <windows.h>
#include <chrono>
#include <iostream>
using namespace x360n;
namespace fs=std::filesystem;
static std::wstring Quote(const fs::path& p){return L"\""+p.wstring()+L"\"";}
int wmain(int argc,wchar_t** argv) {
 if(argc!=3) return 2;
 fs::path root=argv[1], out=argv[2];
 std::ifstream expected_file(out/"expected-library-path.txt");
 std::string expected_text;std::getline(expected_file,expected_text);
 fs::path expected=fs::u8path(expected_text);
 constexpr auto hash="db40df605ade49a612b35a7a24c38f6004bcb17a88ed6b48288de16df9e3987c";
 auto identity=DigestFile(expected);
 if(!identity || identity->sha256!=hash) return 3;
 SessionWindow window;
 std::ofstream evidence(out/"cycle.log");
 evidence<<"diagnostic=true hwnd="<<window.handle()<<std::endl;
 HANDLE job=CreateJobObjectW(nullptr,nullptr);
 JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
 limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
 if(!job || !SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) return 4;
 auto start=std::chrono::steady_clock::now();
 int result=0;
 for(unsigned step=0;step<3;++step) {
  bool game=step==1;
  auto name=game?L"forza_horizon":L"dash_17559";
  auto bin=out/(game?"game-bin":"dash-bin");
  auto exe=bin/(std::wstring(name)+L".exe");
  auto run=out/("step-"+std::to_string(step));fs::create_directories(run);
  auto handoff=run/"handoff.bin";
  auto game_root=root/(game?".local/inputs/games/forza_horizon/disc-1":".local/inputs/dashboards/17559/runtime");
  std::wstring cmd=Quote(exe)+L" --gpu_plugin=xenos --mnk_mode=true --parent_window="+std::to_wstring(window.handle())+
   L" --game_data_root="+Quote(game_root)+L" --user_data_root="+Quote(out/(game?"game-user":"dash-user"))+
   L" --x360n_profile_root="+Quote(out/"dash-user")+L" --x360n_handoff_out="+Quote(handoff);
  if(game) cmd+=L" --x360n_system_ui_host="+Quote(out/"dash-bin/dash_17559_session_ui.dll")+
    L" --x360n_system_ui_root="+Quote(root/".local/inputs/dashboards/17559/runtime");
  else cmd+=L" --dash17559_boot_intro=false";
  if(step) cmd+=L" --x360n_handoff_in="+Quote(out/("step-"+std::to_string(step-1))/"handoff.bin");
  SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
  HANDLE log=CreateFileW((run/"stdout.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,0,nullptr);
  STARTUPINFOW si{sizeof(si)};si.dwFlags=STARTF_USESTDHANDLES;si.hStdOutput=si.hStdError=log;
  si.hStdInput=GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  if(log==INVALID_HANDLE_VALUE || !CreateProcessW(exe.c_str(),cmd.data(),nullptr,nullptr,TRUE,
      CREATE_NO_WINDOW|CREATE_SUSPENDED,nullptr,run.c_str(),&si,&pi)){result=5;break;}
  if(!AssignProcessToJobObject(job,pi.hProcess)){TerminateProcess(pi.hProcess,6);result=6;break;}
  ResumeThread(pi.hThread);CloseHandle(pi.hThread);CloseHandle(log);
  evidence<<"step="<<step<<" pid="<<pi.dwProcessId<<" hwnd="<<window.handle()<<std::endl;
  {std::ofstream state(out/"state.txt");state<<step<<' '<<pi.dwProcessId<<' '<<window.handle()<<std::endl;}
  bool timeout=false;
  while(WaitForSingleObject(pi.hProcess,200)==WAIT_TIMEOUT) {
   if(std::chrono::steady_clock::now()-start>std::chrono::seconds(600)){timeout=true;break;}
  }
  if(timeout){TerminateProcess(pi.hProcess,124);WaitForSingleObject(pi.hProcess,5000);}
  DWORD exit=0;GetExitCodeProcess(pi.hProcess,&exit);CloseHandle(pi.hProcess);
  evidence<<"step="<<step<<" exit="<<exit<<" timeout="<<timeout<<" attachments="<<window.attached_count()<<std::endl;
  if(timeout || exit){result=7;break;}
  LaunchRequest request;
  if(!ReadLaunchRequest(handoff,request)){result=8;break;}
  evidence<<"kind="<<request.kind<<" flags="<<request.flags<<" data="<<request.data.size()<<" path="<<request.path<<std::endl;
  if(step==0) {
   std::error_code ec;auto requested=fs::u8path(request.path);
   auto digest=DigestFile(requested);
   if(request.kind!=LaunchRequest::Image || request.flags || !fs::equivalent(expected,requested,ec) || ec ||
      !digest || digest->sha256!=hash){result=9;break;}
  } else if(step==1) {
   if(request.kind!=LaunchRequest::Dashboard || (request.flags!=0 && request.flags!=0x100)){result=10;break;}
  } else if(request.kind!=LaunchRequest::WindowClose){result=11;break;}
 }
 CloseHandle(job);
 evidence<<"result="<<result<<" (final window close is cleanup, not Xbox Home)"<<std::endl;
 return result;
}
