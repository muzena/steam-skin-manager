#include <gtkmm.h>
#include <random>
#include <future>
#include <glibmm/threads.h>

#include "WebView.cpp"
#include "ManualEditor.cpp"
#include "Reflection.cpp"
#include "AboutWindow.cpp"

#include "MainWindow.h"


using namespace std;

MainWindow::MainWindow(int argc, char* argv[], Settings & linked_settings): settings(linked_settings), steamlaunch(nullptr) {
	kit = Gtk::Application::create(argc, argv);
	
	try {
		builder = Gtk::Builder::create_from_file(settings.get_working_path() + settings.Key("ui_path"));
	}  
	catch(const Glib::FileError &ex)	{
		std::cerr << ex.what() << std::endl;
	}
	
	#include "MainWindowBuilder.connect"
	
	// Signals
	menu_quit->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::Quit));
	menu_about->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::LaunchAboutWindow));
	create_launcher->signal_activate().connect(sigc::mem_fun(*this, &MainWindow::CreateLauncher));
	menu_run_steam->signal_activate().connect(sigc::group(sigc::mem_fun(*this, &MainWindow::SteamLauncherThread), false));
	menu_run_steam_wb->signal_activate().connect(sigc::group(sigc::mem_fun(*this, &MainWindow::SteamLauncherThread), true));
	launch_steam_from_toolbar->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::SteamLauncher));
	apply->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::ApplyTheme));
	use_decorations->property_active().signal_changed().connect(sigc::mem_fun(*this, &MainWindow::SetPreviewName));
	notebook->signal_switch_page().connect(sigc::mem_fun(*this, &MainWindow::PageSwitched));
	
	//Translatable init strings
	#include "translate_gui.strings"
	if(settings.Key("remove_launcher_generator") == "yes") {
		create_launcher->hide();
	}
	
	// Combobox settings
	ListAvaialbleThemes(settings.get_working_path() + settings.GetPath("theme"));
	ListAvaialbleThemes(settings.Key("old_theme_path")); /* Steam Skin Manager (3.x) and older backward compatibility */
	ListAvaialbleThemes(settings.GetLocalPath() + "themes/"); /* Steam Skin Manager (4.3+) user's reflection themes */
	
	// State Change signals
	comboboxsignal = comboboxthemes->signal_changed().connect(sigc::group(sigc::mem_fun(*this, &MainWindow::PreviewTheme), true));
	file_chooser->signal_file_set().connect(sigc::group(sigc::mem_fun(*this, &MainWindow::PreviewTheme), false));

	// Initialize Toolbar buttons
	SetToolButtonImage(launch_steam_from_toolbar, settings.get_working_path() + "share/steam-skin-manager/images/logo.svg");
	SetToolButtonSize(prev_tip);
	SetToolButtonSize(next_tip);
	
	skin = new Skin(settings); // Create skin class	
	PreviewTheme(); // Define theme using user's input
    ShowTips();
	
	// Load other tabs
	WebView web(builder, settings);
	manualeditor = new ManualEditor(builder, settings);
	reflection = nullptr;
	if(settings.Key("preview") != "true") {
		notebook->remove_page(2);
	}
	
	if(window) {
		// SystemColors
		Glib::RefPtr<Gtk::StyleContext> toolbarcontext = toolbar->get_style_context();
		toolbarcontext->add_class("primary-toolbar");
		Glib::RefPtr<Gtk::StyleContext> framecontext = frame->get_style_context();
		framecontext->add_class("primary-toolbar");
		
		window->set_title(settings.GetApplicationName().c_str());
		
		Glib::RefPtr<Gdk::Screen> screen = Gdk::Screen::get_default(); 
		int height = screen->get_height();
		int local_height, na;
		int pluselement = 120;
		main_box->get_preferred_height(na, local_height);
		if(height < local_height + pluselement) {
			window->set_default_size(500, height - 100);
		}
		else {
			window->set_default_size(800, local_height + pluselement);
		}
		kit->run(*window);
	}
	else {
		throw runtime_error("Window not initialized");
	}
}

void MainWindow::PageSwitched(Gtk::Widget *page, guint page_num) {
	if(reflection == nullptr && page_num == 2) {
		reflection = new Reflection(builder, settings);
	}
	if(page_num == 0) {
		ListAvaialbleThemes(settings.GetLocalPath() + "themes/", false); /* Steam Skin Manager (4.3+) user's reflection themes */
	}
}

void MainWindow::SetToolButtonSize(Gtk::ToolButton* button, int sizeX, int sizeY) {
	if(sizeX < 0 && sizeY < 0) {
		Gtk::IconSize::lookup(button->get_icon_size(), sizeX, sizeY);
		button->set_size_request(sizeX + 40, -1);
	}
	else {
		button->set_size_request(sizeX, sizeY);
	}
}

void MainWindow::SetToolButtonImage(Gtk::ToolButton* button, string file) {
	Glib::RefPtr<Gdk::Pixbuf> pix = Gdk::Pixbuf::create_from_file(file);
	int iconW, iconH;
	Gtk::IconSize::lookup(button->get_icon_size(), iconW, iconH);
	logo->set(pix->scale_simple(iconW + 10, iconH + 10, Gdk::INTERP_BILINEAR));
	SetToolButtonSize(button);
}

void MainWindow::LaunchAboutWindow() {
	AboutWindow about(builder, settings);
}

void MainWindow::ApplyTheme() {
	SpinnerStart();
	string skinpath;
	
	if(skin->GetName() == defaultskin) {
		skinpath = skin->GetPath(true);
		if(settings.RevertSkin()) {
			notify->set_markup(_("Theme was successfully reverted."));
			notify->override_color(Gdk::RGBA("#0a0"));
		}
		else {
			notify->set_markup(_("Steam Skin Manager could not revert the theme."));
			notify->override_color(Gdk::RGBA("#a00"));
			cerr << "Can't revert to the default due to a missing execution shell script." << endl;
		}
	}
	else {	
		bool status_code = settings.SetSkin(skin->GetPath(false, !use_decorations->get_active()));
		
		if(status_code != -1 && status_code != 127) {
			notify->set_markup(_("<b>Theme was applied.</b>"));
			notify->override_color(Gdk::RGBA("#3a3"));
		}
		else {
			notify->set_markup(_("<b>Steam Skin Manager could not set the theme.</b>"));
			notify->override_color(Gdk::RGBA("#a33"));
		}
	}
	
    SpinnerStop();
}

void MainWindow::PreviewTheme(bool native) {
	SpinnerStart();
	string skin_name, skin_path;
	
	if(native || file_chooser->get_filename() == "") {
		file_chooser->unselect_all();
		if(comboboxthemes->get_active_row_number() < 0) {
			return;
		}
		skin_name = comboboxthemes->get_active_text();
		skin_path = bundledSkins[comboboxthemes->get_active_row_number()];
	}
	else {
		comboboxsignal.block();
		comboboxthemes->unset_active();
		comboboxsignal.unblock();
		skin_path = file_chooser->get_filename();
		size_t found = skin_path.find_last_of("/\\");
		skin_name = skin_path.substr(found + 1);
	}
	
	if(skin_name == defaultskin) {
		skin->FillDefault(defaultskin);
	}
	else {
		skin->Fill(skin_name, skin_path);
	}
	
	if(skin->HasVariants()) {
		use_decorations->set_sensitive(true);
		if(skin->GetVariants() != 111) {
			if(skin->GetVariants() == 110) {
				use_decorations->set_sensitive(false);
				use_decorations->set_active(false);
			}
			else if(skin->GetVariants() == 101) {
				use_decorations->set_sensitive(false);
				use_decorations->set_active(true);
			}
		}
	}
	else {
		use_decorations->set_sensitive(false);
		use_decorations->set_active(false);
	}
	
	SetPreviewName();
	SetPreviewImage();
	SpinnerStop();
}

void MainWindow::SetPreviewName() {
	string namelabel = " " + skin->GetName();
	if(namelabel.length() > 30) {
		namelabel = " ... ";
	}
	else {
		namelabel = _("<b>Currently selected:</b>") + namelabel;
	}
	if(skin->HasVariants()) {
		namelabel += ", ";
		if(use_decorations->get_active()) {
			namelabel += _("With decorations");
		}
		else {
			namelabel += _("Without decorations");
		}
	}
	skin_name_label->set_markup(namelabel);
	preview_name_box->set_visible_window(false);
}

void MainWindow::SetPreviewImage() {
	preview_description->set_line_wrap(true);
	
	try {
		Glib::RefPtr<Gdk::Pixbuf> tmp_image = Gdk::Pixbuf::create_from_file(skin->GetImg());
		tmp_image = tmp_image->scale_simple(512, 314, Gdk::INTERP_BILINEAR);
		preview_image->set(tmp_image);
	}
	catch(...) {
		cerr << "Couldn't load a preview image." << endl;
	}
	
	if(skin->GetDescription() != "") {
		preview_description->set_markup(skin->GetDescription());
	}
}

void MainWindow::ShowTips() {
	notify->set_markup(settings.GetTip(true));
	
	prev_tip->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::PreviousTip));
	next_tip->signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::NextTip));
}

void MainWindow::NextTip() {
	notify->set_markup(settings.GetTip(true));
	notify->unset_color();
}

void MainWindow::PreviousTip() {
	notify->set_markup(settings.GetTip(false));
	notify->unset_color();
}

void MainWindow::LaunchSteam(bool decorations) {
	Steam steam(settings);
	steam.Run(decorations);
}

void MainWindow::SteamLauncherThread(bool decorations) {
	SpinnerStart();
	launch_steam_from_toolbar->set_sensitive(false);
	launch_steam_from_toolbar->set_opacity(0.4);
	menu_run_steam->set_sensitive(false);
	menu_run_steam_wb->set_sensitive(false);
	steamlaunch = new future<void>(async(launch::async, [&] {
		LaunchSteam(decorations);
		gdk_threads_add_idle(reinterpret_cast<GSourceFunc>(&MainWindow::SteamFinished), this);
    }));
}

void MainWindow::SteamFinished(gpointer object) {
	MainWindow *self = static_cast<MainWindow*>(object);
	self->launch_steam_from_toolbar->set_sensitive(true);
	self->launch_steam_from_toolbar->set_opacity(1);
	self->menu_run_steam->set_sensitive(true);
	self->menu_run_steam_wb->set_sensitive(true);
	self->SpinnerStop();
	self->kit->activate();
}

void MainWindow::SteamLauncher() {
	SteamLauncherThread(use_decorations->get_active());
}

void MainWindow::ListAvaialbleThemes(string path, bool repeat) {
	vector<string> test = settings.GetListOfAvaialbleFolders(path);
	string theme = settings.GetSystemTheme();
	
	int i = bundledSkins.size();
	if(bundledSkins.empty() || bundledSkins.front() != defaultskin) {	
		comboboxthemes->insert(0, defaultskin);
		bundledSkins.push_back(defaultskin);
		comboboxthemes->set_active(i);
		i++;
	}
	
	for(vector<string>::iterator it = test.begin(); it != test.end(); ++it) {
		if(!repeat) {
			if(find(bundledSkins.begin(), bundledSkins.end(), path + string(*it)) != bundledSkins.end()) {
				continue;
			}
		}
		
		comboboxthemes->insert(i, (*it));
		bundledSkins.push_back(path + (*it));
		
		if(string(*it) == theme)
			comboboxthemes->set_active(i);
		i++;
	}
}

void MainWindow::SpinnerStart() {
	spinner->start();
}

void MainWindow::SpinnerStop() {
	spinner->stop();
}

void MainWindow::CreateLauncher() {
	settings.CreateLauncher();
}

void MainWindow::Quit() {
	window->hide();
}

MainWindow::~MainWindow() {
	delete manualeditor;
	delete skin;
	delete steamlaunch;
	if(reflection != nullptr) {
		delete reflection;
	}
	cout << "Bye Bye " << getenv("USER") << " ..." << endl;
}
