//! このプロジェクトで使うインターフェースのIIDをインスタンス化するために、
//! このマクロを有効にして一度だけインクルードする
#define INIT_CLASS_IID

#include "vst3/pluginterfaces/test/itest.h"
#include "vst3/pluginterfaces/base/ibstream.h"
#include "vst3/pluginterfaces/base/ipluginbase.h"
#include "vst3/pluginterfaces/base/funknown.h"
#include "vst3/pluginterfaces/gui/iplugview.h"

#include "vst3/base/source/fobject.h"
#include "vst3/base/source/fstring.h"
#include "vst3/base/source/fcontainer.h"