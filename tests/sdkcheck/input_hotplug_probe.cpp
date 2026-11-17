// Headless reproduction of simultaneous title/HUD/capability polling while
// synthetic devices disappear and acquire new identities. No physical input or window.
#include <rex/app/app.h>
#include <rex/apu/audio_system.h>
#include <rex/gpu/graphics_system.h>
#include <rex/hid/input_driver.h>
#include <rex/hid/input_system.h>
#include <rex/driver_module.h>
#include <rex/ui/ui_system.h>
#include <array>
#include <atomic>
#include <barrier>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace rex::hid;
namespace {
class HotplugPad final : public IInputDriver {
 public:
  std::string_view GetName() const override {return "hotplug";}
  rex::Result<void> Initialize(rex::IDriverContext&) override {return {};}
  void Shutdown() override {}
  rex::u64 GetDeviceRevision() const override {return generation.fetch_add(1);}
  void EnumerateDevices(std::vector<PadDevice>& out) override {
    out.clear();const auto n=generation.fetch_add(1);
    for(unsigned i=0;i<n%5;++i) {
      PadDevice pad;pad.id=PadDeviceID{1+(n/5)*4+i};pad.name="hotplug fixture";
      out.push_back(std::move(pad));
    }
  }
  rex::Result<PadState> GetDeviceState(PadDeviceID) override {return PadState{};}
  rex::Result<PadCapabilities> GetDeviceCapabilities(PadDeviceID) override {return PadCapabilities{};}
  rex::Result<void> SetDeviceVibration(PadDeviceID,const PadVibration&) override {return {};}
 private:
  mutable std::atomic<unsigned> generation{1};
};
std::unique_ptr<rex::IDriver> CreatePad() {return std::make_unique<HotplugPad>();}
class Provider final : public rex::IModule,public rex::IDriverModule {
 public:
  std::string_view GetName() const override {return "hotplug_probe";}
  rex::Result<void> Connect(rex::IInterfaceFactory&) override {return {};}
  void Disconnect() override {}
  rex::Result<void> Init() override {return {};}
  void Shutdown() override {}
  rex::IInterface* QueryInterface(std::string_view name,rex::u32 version) override {
    return rex::FindModuleInterface<rex::IDriverModule>(*this,name,version);
  }
  std::span<const rex::DriverRegistration> GetDrivers() const override {return drivers;}
 private:
  static constexpr std::array drivers{rex::DriverRegistration{rex::DriverType::kInput,"hotplug",{},CreatePad}};
};
class HotplugApp final : public rex::app::App {
 public:
  explicit HotplugApp(std::filesystem::path scratch):App("input_hotplug_probe"),scratch_(std::move(scratch)) {}
  ~HotplugApp() override {Shutdown();}
 protected:
  void OnRegisterModules(std::vector<rex::IModule*>& modules) override {
    App::OnRegisterModules(modules);modules.push_back(&provider_);
  }
  rex::Result<void> OnConfigure(rex::SettingsRegistry& settings) override {
    REX_TRY_VOID(App::OnConfigure(settings));
    REX_TRY_VOID(root.SetText(rex::SettingLayer::kTitle,scratch_.string()));
    REX_TRY_VOID(GetInput().GetSettings().drivers.SetText(rex::SettingLayer::kTitle,"hotplug"));
    REX_TRY_VOID(GetUI().GetSettings().driver.SetText(rex::SettingLayer::kTitle,"none"));
    REX_TRY_VOID(GetAudio().GetSettings().driver.SetText(rex::SettingLayer::kTitle,"none"));
    return GetGraphics().GetSettings().driver.SetText(rex::SettingLayer::kTitle,"none");
  }
 private:
  std::filesystem::path scratch_;Provider provider_;
};
}
int main() {
  const auto scratch=std::filesystem::temp_directory_path()/
      ("x360n-input-hotplug-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(scratch);
  struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{scratch};
  HotplugApp app(scratch);
  const std::vector<std::string> args{"--app.config_file=","--log.level=error"};
  if(!app.Init(rex::CLIArgList::Parse(args)))return 2;
  auto& input=app.GetInput();
  std::barrier start(4);std::array<std::thread,4> pollers;std::atomic<unsigned> completed{0};
  std::cout<<"Four input consumers; 20000 hotplug queries each\n"<<std::flush;
  for(unsigned user=0;user<4;++user)pollers[user]=std::thread([&,user] {
    start.arrive_and_wait();
    for(unsigned i=0;i<20000;++i) {
      switch(user) {
        case 0: (void)input.GetState(user);break;
        case 1: (void)input.GetCapabilities(user);break;
        case 2: (void)input.GetKeystroke(user,KeystrokeSource::kPad);break;
        case 3: (void)input.SetVibration(user,PadVibration{});break;
      }
      ++completed;
    }
  });
  for(auto& poller:pollers)poller.join();
  app.Shutdown();std::cout<<"Completed "<<completed<<" queries\n";
  return completed==80000?0:1;
}
