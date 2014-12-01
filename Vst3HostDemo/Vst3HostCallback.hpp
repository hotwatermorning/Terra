#pragma once

#include <memory>

#include <boost/function.hpp>

#include "vst3/pluginterfaces/base/funknown.h"
#include "vst3/pluginterfaces/vst/ivstparameterchanges.h"
#include "vst3/public.sdk/source/vst/hosting/parameterchanges.h"
#include "./Vst3Utils.hpp"

namespace hwm {

class Vst3HostCallback
{
	typedef boost::function<void(Steinberg::int32 flag)> request_to_restart_handler_t;
	typedef boost::function<void(Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue)> parameter_change_notification_handler_t;

public:
	typedef std::unique_ptr<Steinberg::FUnknown, SelfReleaser> unknown_ptr;

public:
	Vst3HostCallback();
	~Vst3HostCallback();

	unknown_ptr GetUnknownPtr();

	void TakeParameterChanges(Steinberg::Vst::ParameterChanges &param_changes);

	void SetRequestToRestartHandler(request_to_restart_handler_t handler);
	void SetParameterChangeNotificationHandler(parameter_change_notification_handler_t handler);

private:
	struct Impl;
	std::unique_ptr<Impl> pimpl_;
};

} // ::hwm