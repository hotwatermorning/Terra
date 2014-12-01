#include "Keyboard.hpp"

#include <map>
#include <sstream>

#include <boost/function.hpp>
#include <boost/optional.hpp>
#include <boost/range.hpp>

#include <balor/gui/all.hpp>
#include <balor/graphics/all.hpp>
#include "./namespace.hpp"

#include <Windows.h>
#include <windowsx.h>

namespace hwm {

namespace detail {

static int const BLACK_KEY_WIDTH = 10;
static int const WHITE_KEY_WIDTH = 16;
static int const BLACK_KEY_X[] = { 0, 10, 0, 28, 0, 0, 58, 0, 75, 0, 92, 0 };
static int const OCTAVE_WIDTH = WHITE_KEY_WIDTH * 7;
static double const BLACK_KEY_HEIGHT_RATIO = 0.6;
static int const NUM_WHITE_KEY = 75;
static int const ALL_KEY_WIDTH = NUM_WHITE_KEY * WHITE_KEY_WIDTH;

}	//::detail

gpx::Color Keyboard::GetWhiteKeyColor() { return gpx::Color(250, 240, 230); }
gpx::Color Keyboard::GetBlackKeyColor() { return gpx::Color(5, 5, 5); }
gpx::Color Keyboard::GetKeyFrameColor() { return gpx::Color(100, 220, 230); }
gpx::Color Keyboard::GetSelectedWhiteKeyColor() { return gpx::Color(255, 200, 140); }
gpx::Color Keyboard::GetSelectedBlackKeyColor() { return gpx::Color(225, 160, 120); }

Keyboard::Keyboard()
{}

Keyboard::Keyboard(gui::Control &parent, int x, int y, int width, int height)
	: wnd_(parent, x, y, width, height)
	, font_key_name_(L"Arial", 7, gpx::Font::Style::regular, gpx::Font::Quality::default)
	, bitmap_(width, height)
	, gpx_(bitmap_)
	, timer_(wnd_, 1)
	, softkey_note_on_(128)
	, softkey_base_(0x3c)
	, note0_pos_(detail::OCTAVE_WIDTH * (-3))
	, oct_down_pressed_(false)
	, oct_up_pressed_(false)
{
	wnd_.onPaint()		= [this] (gui::Panel::Paint &e) { OnPaint(e); };
	wnd_.onMouseDown()	= [this] (gui::Panel::MouseDown &e) { OnMouseDown(e); };
	wnd_.onMouseMove()	= [this] (gui::Panel::MouseMove &e) { OnMouseMove(e); };
	wnd_.onMouseUp()	= [this] (gui::Panel::MouseUp &e) { OnMouseUp(e); };

	timer_.onRun() = [this](gui::Timer::Run &) { CheckKeyState(); };
	timer_.start();
}

//! 鍵盤が押されたり離されたりした時に呼ばれる
//! コールバックを設定する
void Keyboard::SetNoteHandler(note_handler_t handler)
{
	note_handler_ = handler;
}

void Keyboard::OnPaint(gui::Panel::Paint &e)
{
	gpx_.pen(gpx::Color::black());
		
	int const height = wnd_.size().height;
	int const width = wnd_.size().width;

	int const white_key_height = height;
	int const black_key_height = static_cast<int>(white_key_height * detail::BLACK_KEY_HEIGHT_RATIO + 0.5);

	auto const white_key_pos_to_note_number = [](int key_pos) {
		int octave = (key_pos / 7) * 12;
		int white_key_index[] = { 0, 2, 4, 5, 7, 9, 11 };
		return octave + white_key_index[key_pos % 7];
	};

	auto const note_selected = [this](size_t note_number) {
		return softkey_note_on_[note_number] || mouse_note_on_ == note_number;
	};

	gpx_.backTransparent(true);

	// draw keyboard
	gpx_.pen(GetKeyFrameColor());

	for(int i = 0; i < detail::NUM_WHITE_KEY; ++i) {
		int const left = note0_pos_ + (detail::WHITE_KEY_WIDTH * i);
		int const right = left + detail::WHITE_KEY_WIDTH;
		if(0 < right && left < width) {

			int note_number = white_key_pos_to_note_number(i);
			if(note_selected(note_number)) {
				gpx_.brush(GetSelectedWhiteKeyColor());
			} else {
				gpx_.brush(GetWhiteKeyColor());
			}
			gpx_.drawRectangle(left, 0, detail::WHITE_KEY_WIDTH, white_key_height);

			if(i % 7 == 0) {
				std::wstringstream ss;
				ss << "C" << ((i / 7) - 2);
				gpx_.font(font_key_name_);
				gpx_.drawText(
					ss.str(),
					left,
					white_key_height - 20,
					detail::WHITE_KEY_WIDTH - 1,
					20 - 1,
					gpx::Graphics::TextFormat::horizontalCenter|gpx::Graphics::TextFormat::verticalCenter
					);
			}
		}
	}

	for(int i = 0; i < 128; ++i) {
		switch(i % 12) {
			case 1: case 3: case 6: case 8: case 10: {
				int const left = note0_pos_ + detail::BLACK_KEY_X[i % 12] + (i / 12 * detail::OCTAVE_WIDTH);
				int const right = left + detail::BLACK_KEY_WIDTH;

				if(0 < right && left < width) {

					if(note_selected(i)) {
						gpx_.brush(GetSelectedBlackKeyColor());
						gpx_.pen(GetSelectedBlackKeyColor());
					} else {
						gpx_.brush(GetBlackKeyColor());
						gpx_.pen(GetBlackKeyColor());
					}

					gpx_.drawRectangle(left, 0, detail::BLACK_KEY_WIDTH, black_key_height);
				}
			}
			break;
		}
	}

	// オフスクリーンバッファをコピー
	e.graphics().copy(0, 0, gpx_);
}

boost::optional<size_t> 
	Keyboard::GetNoteNumber(balor::Point const &pt) const
{
	int const black_kies[] = { 1, 3, 6, 8, 10 };
	int const white_kies[] = { 0, 2, 4, 5, 7, 9, 11 };
	int const white_key_height = wnd_.size().height;
	int const black_key_height = static_cast<int>(white_key_height * detail::BLACK_KEY_HEIGHT_RATIO + 0.5);

	int const x = pt.x - note0_pos_;
	if(x < 0 || detail::WHITE_KEY_WIDTH * detail::NUM_WHITE_KEY <= x) {
		return boost::none;
	}
	int const oct = x / detail::OCTAVE_WIDTH * 12;

	int const last_black_key_right_pos =
		detail::OCTAVE_WIDTH * 10 + detail::BLACK_KEY_X[6] + detail::BLACK_KEY_WIDTH;

	// xの位置が最後の白鍵の場合は黒鍵なし
	if(pt.y <= black_key_height && x < last_black_key_right_pos) {
		int const x_in_oct = x % detail::OCTAVE_WIDTH;
		for(int i : black_kies) {
			if(detail::BLACK_KEY_X[i] <= x_in_oct && x_in_oct <= detail::BLACK_KEY_X[i] + detail::BLACK_KEY_WIDTH) {
				int const at = i + oct;
				BOOST_ASSERT(0 <= at && at <= 127);
				return at;
			}
		}
	}

	//! 白鍵の位置。（黒鍵の隙間を含む）
	int const at = white_kies[(x / detail::WHITE_KEY_WIDTH) % 7] + oct;
	BOOST_ASSERT(0 <= at && at <= 127);
	return at;
}

void Keyboard::OnMouseDown(gui::Panel::MouseDown &e)
{
	if(e.button() != gui::Mouse::lButton || e.ctrl() || e.shift()) {
		return;
	}

	if(::GetAsyncKeyState(VK_SPACE) < 0) {
		BeginKeyboardViewShift(e.position().x);
		return;
	}

	auto note_number = GetNoteNumber(e.position());
	if(!note_number) {
		return;
	}

	e.sender().captured(true);

    //! プラグインにノートオンを設定
	note_handler_(*note_number, true);
	mouse_note_on_ = note_number;
	wnd_.invalidate();
}

void Keyboard::OnMouseMove(gui::Panel::MouseMove &e)
{
	if(IsKeyboardViewShiftStarted()) {
		ShiftKeyboardView(e.position().x);
		return;
	}

	if(!mouse_note_on_) {
		return;
	}

	auto note_number = GetNoteNumber(e.position());
	if(!note_number) {
		return;
	}

	if(mouse_note_on_ == note_number) {
		return;
	}

	note_handler_(*mouse_note_on_, false);
	note_handler_(*note_number, true);
	mouse_note_on_ = note_number;
	wnd_.invalidate();
}

void Keyboard::OnMouseUp(gui::Panel::MouseUp &e)
{
	if(e.button() != gui::Mouse::lButton) {
		return;
	}

	if(IsKeyboardViewShiftStarted()) {
		EndKeyboardViewShift();
		return;
	}

	if(!mouse_note_on_) {
		return;
	}

	if(e.sender().captured()) {
		e.sender().captured(false);
	}


	note_handler_(*mouse_note_on_, false);
	mouse_note_on_ = boost::none;
	wnd_.invalidate();
}

void Keyboard::BeginKeyboardViewShift(int x)
{
	wnd_.captured(true);
	shift_from_ = x;
	stored_note0_pos_ = note0_pos_;
}

void Keyboard::EndKeyboardViewShift()
{
	shift_from_ = boost::none;
	wnd_.captured(true);
}

void Keyboard::ShiftKeyboardView(int x)
{
	int shift_width = x - *shift_from_;
	int new_pos = 0;
	if(shift_width + stored_note0_pos_ > 0) {
		new_pos = 0;
	} else if(shift_width + stored_note0_pos_ + detail::ALL_KEY_WIDTH < wnd_.size().width) {
		new_pos = wnd_.size().width - detail::ALL_KEY_WIDTH;
	} else {
		new_pos = shift_width + stored_note0_pos_;
	}

	if(new_pos != note0_pos_) {
		note0_pos_ = new_pos;
		wnd_.invalidate();
	}
}

std::map<gui::Key, int>
	Keyboard::GenerateKeyMap()
{
	typedef std::pair<gui::Key, int> pair_t;
	pair_t keymap[] = {
		pair_t( gui::Key::a, 0 ),
		pair_t( gui::Key::w, 1 ),
		pair_t( gui::Key::s, 2 ),
		pair_t( gui::Key::e, 3 ),
		pair_t( gui::Key::d, 4 ),
		pair_t( gui::Key::f, 5 ),
		pair_t( gui::Key::t, 6 ),
		pair_t( gui::Key::g, 7 ),
		pair_t( gui::Key::y, 8 ),
		pair_t( gui::Key::h, 9 ),
		pair_t( gui::Key::u, 10 ),
		pair_t( gui::Key::j, 11 ),
		pair_t( gui::Key::k, 12 ),
		pair_t( gui::Key::o, 13 ),
		pair_t( gui::Key::l, 14 ),
		pair_t( gui::Key::p, 15 )
	};

	// Visual Studio 2012ではSTLがinitializer listに対応していないので、
	// pairの配列を使用して初期化している
	return std::map<gui::Key, int>(boost::begin(keymap), boost::end(keymap));
}

std::map<gui::Key, int> const &
	Keyboard::GetKeyMap() const
{
	static std::map<gui::Key, int> const keymap = GenerateKeyMap();
	return keymap;
}

//! このウィンドウのタイマーから呼び出される
//! キーボードの状態をチェックして、ノート信号の送信や、
//! オクターブ設定の変更を行ったりする。
void Keyboard::CheckKeyState()
{
	static std::map<gui::Key, int> const &keymap = GetKeyMap();
	if(::GetAsyncKeyState(gui::Key::z) < 0) {
		if(!oct_down_pressed_) {
			oct_down_pressed_ = true;
			if(softkey_base_ - 12 >= 0) {
				softkey_base_ -= 12;
			}
		}
	} else {
		oct_down_pressed_ = false;
	}

	if(::GetAsyncKeyState(gui::Key::x) < 0) {
		if(!oct_up_pressed_) {
			oct_up_pressed_ = true;
			if(softkey_base_ + 12 <= 120) {
				softkey_base_ += 12;
			}
		}
	} else {
		oct_up_pressed_ = false;
	}

	for(auto const &entry: keymap) {
		int const note_number = entry.second + softkey_base_;
		if(note_number >= 128) { continue; }

		if(::GetAsyncKeyState(entry.first) < 0) {
			if(softkey_note_on_[note_number] == false) {
				softkey_note_on_[note_number] = true;
				note_handler_(note_number, true);
				wnd_.invalidate();
			}
		} else {
			if(softkey_note_on_[note_number] == true) {
				softkey_note_on_[note_number] = false;
				note_handler_(note_number, false);
				wnd_.invalidate();
			}
		}
	}
}

bool Keyboard::IsKeyboardViewShiftStarted() const { return static_cast<bool>(shift_from_); }

} // ::hwm