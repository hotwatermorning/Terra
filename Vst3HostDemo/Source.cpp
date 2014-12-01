#include <windows.h>
#include <tchar.h>

#include <balor/gui/all.hpp>
#include <balor/locale/all.hpp>
#include "namespace.hpp"

#include "VstHostDemo.hpp"
#include "./Vst3Plugin.hpp"

namespace hwm {

static size_t const sampling_rate = 44100;
static size_t const block_size = 1024;
static size_t const num_channels = 2;

int main_impl()
{
	VstHostDemo demo_application_;

	bool const opened = demo_application_.OpenDevice(sampling_rate, num_channels, block_size);
	if(!opened) {
		return -1;
	}

	demo_application_.Run();

	return 0;
}

}	//::hwm

int APIENTRY WinMain(HINSTANCE , HINSTANCE , LPSTR , int ) {

	try {
		hwm::main_impl();
	} catch(std::exception &e) {
		balor::gui::MessageBox::show(
			balor::String(
				_T("error : ")) + 
				balor::locale::Charset(932, true).decode(e.what())
				);
	}
}
