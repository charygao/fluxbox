// Window.cc for Fluxbox Window Manager
// Copyright (c) 2001 - 2003 Henrik Kinnunen (fluxgen at users.sourceforge.net)
//
// Window.cc for Blackbox - an X11 Window manager
// Copyright (c) 1997 - 2000 Brad Hughes (bhughes at tcac.net)
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.

// $Id: Window.cc,v 1.162 2003/05/08 01:51:18 rathnor Exp $

#include "Window.hh"

#include "WinClient.hh"
#include "i18n.hh"
#include "fluxbox.hh"
#include "Screen.hh"
#include "StringUtil.hh"
#include "Netizen.hh"
#include "FbWinFrameTheme.hh"
#include "MenuTheme.hh"
#include "TextButton.hh"
#include "EventManager.hh"
#include "FbAtoms.hh"
#include "RootTheme.hh"
#include "Workspace.hh"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif // HAVE_CONFIG_H

//use GNU extensions
#ifndef	 _GNU_SOURCE
#define	 _GNU_SOURCE
#endif // _GNU_SOURCE

#include <X11/Xatom.h>
#include <X11/keysym.h>

#include <cstring>
#include <cstdio>
#include <iostream>
#include <cassert>

using namespace std;

namespace {

void grabButton(Display *display, unsigned int button, 
                Window window, Cursor cursor) {

    //numlock
    XGrabButton(display, button, Mod1Mask|Mod2Mask, window, True,
                ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                GrabModeAsync, None, cursor);
    //scrolllock
    XGrabButton(display, button, Mod1Mask|Mod5Mask, window, True,
                ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                GrabModeAsync, None, cursor);
	
    //capslock
    XGrabButton(display, button, Mod1Mask|LockMask, window, True,
                ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                GrabModeAsync, None, cursor);

    //capslock+numlock
    XGrabButton(display, Button1, Mod1Mask|LockMask|Mod2Mask, window, True,
                ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                GrabModeAsync, None, cursor);

    //capslock+scrolllock
    XGrabButton(display, button, Mod1Mask|LockMask|Mod5Mask, window, True,
                ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                GrabModeAsync, None, cursor);
	
    //capslock+numlock+scrolllock
    XGrabButton(display, button, Mod1Mask|LockMask|Mod2Mask|Mod5Mask, window, 
                True,
                ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                GrabModeAsync, None, cursor);

    //numlock+scrollLock
    XGrabButton(display, button, Mod1Mask|Mod2Mask|Mod5Mask, window, True,
                ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
                GrabModeAsync, None, cursor);
	
}

// X event scanner for enter/leave notifies - adapted from twm
typedef struct scanargs {
    Window w;
    Bool leave, inferior, enter;
} scanargs;

// look for valid enter or leave events (that may invalidate the earlier one we are interested in)
static Bool queueScanner(Display *, XEvent *e, char *args) {
    if (e->type == LeaveNotify &&
        e->xcrossing.window == ((scanargs *) args)->w &&
        e->xcrossing.mode == NotifyNormal) {
        ((scanargs *) args)->leave = true;
        ((scanargs *) args)->inferior = (e->xcrossing.detail == NotifyInferior);
    } else if (e->type == EnterNotify &&
               e->xcrossing.mode == NotifyUngrab)
        ((scanargs *) args)->enter = true;

    return false;
}

/// returns the deepest transientFor, asserting against a close loop
WinClient *getRootTransientFor(WinClient *client) {
    while (client->transientFor()) {
        assert(client != client->transientFor());
        client = client->transientFor();
    }
    return client;
}


/// raise window and do the same for each transient of the current window
void raiseFluxboxWindow(FluxboxWindow &win) {
    if (win.oplock) return;
    win.oplock = true;

    if (!win.isIconic()) {
        win.getScreen().updateNetizenWindowRaise(win.getClientWindow());
        win.getLayerItem().raise();
    }

    // for each transient do raise
    WinClient::TransientList::const_iterator it = win.winClient().transientList().begin();
    WinClient::TransientList::const_iterator it_end = win.winClient().transientList().end();
    for (; it != it_end; ++it) {
        if ((*it)->fbwindow() && !(*it)->fbwindow()->isIconic())
            // TODO: should we also check if it is the active client?
            raiseFluxboxWindow(*(*it)->fbwindow());
    }
    win.oplock = false;
}

/// lower window and do the same for each transient it holds
void lowerFluxboxWindow(FluxboxWindow &win) {
    if (win.oplock) return;
    win.oplock = true;

    if (!win.isIconic()) {
        win.getScreen().updateNetizenWindowLower(win.getClientWindow());
        win.getLayerItem().lower();
    }

    WinClient::TransientList::const_iterator it = win.winClient().transientList().begin();
    WinClient::TransientList::const_iterator it_end = win.winClient().transientList().end();
    for (; it != it_end; ++it) {
        if ((*it)->fbwindow() && !(*it)->fbwindow()->isIconic())
            // TODO: should we also check if it is the active client?
            lowerFluxboxWindow(*(*it)->fbwindow());
    }
    win.oplock = false;
}

/// raise window and do the same for each transient it holds
void tempRaiseFluxboxWindow(FluxboxWindow &win) {
    if (win.oplock) return;
    win.oplock = true;

    if (!win.isIconic()) {
        // don't update netizen, as it is only temporary
        win.getLayerItem().tempRaise();
    }

    // for each transient do raise
    WinClient::TransientList::const_iterator it = win.winClient().transientList().begin();
    WinClient::TransientList::const_iterator it_end = win.winClient().transientList().end();
    for (; it != it_end; ++it) {
        if ((*it)->fbwindow() && !(*it)->fbwindow()->isIconic())
            // TODO: should we also check if it is the active client?
            tempRaiseFluxboxWindow(*(*it)->fbwindow());
    }
    win.oplock = false;
}

class SetClientCmd:public FbTk::Command {
public:
    explicit SetClientCmd(WinClient &client):m_client(client) {
    }
    void execute() {
        if (m_client.m_win != 0)
            m_client.m_win->setCurrentClient(m_client);
    }
private:
    WinClient &m_client;
};

};

template <>
void LayerMenuItem<FluxboxWindow>::click(int button, int time) {
    m_object->moveToLayer(m_layernum);
}


FluxboxWindow::FluxboxWindow(WinClient &client, BScreen &scr, FbWinFrameTheme &tm,
                             FbTk::MenuTheme &menutheme, 
                             FbTk::XLayer &layer):
    oplock(false),
    m_hintsig(*this),
    m_statesig(*this),
    m_layersig(*this),
    m_workspacesig(*this),
    m_diesig(*this),
    moving(false), resizing(false), shaded(false), maximized(false),
    iconic(false), focused(false),
    stuck(false), send_focus_message(false), m_managed(false),
    screen(scr),
    timer(this),
    display(0),
    lastButtonPressTime(0),
    m_windowmenu(menutheme, scr.getScreenNumber(), *scr.getImageControl()),
    m_layermenu(menutheme, 
                scr.getScreenNumber(),
                *scr.getImageControl(), 
                *scr.layerManager().getLayer(Fluxbox::instance()->getMenuLayer()), 
                this,
                false),
    old_decoration(DECOR_NORMAL),
    m_client(&client),   
    m_frame(tm, *scr.getImageControl(), scr.getScreenNumber(), 0, 0, 100, 100),
    m_layeritem(m_frame.window(), layer),
    m_layernum(layer.getLayerNum())  {

    init();
}


FluxboxWindow::FluxboxWindow(Window w, BScreen &scr, FbWinFrameTheme &tm,
                             FbTk::MenuTheme &menutheme, 
                             FbTk::XLayer &layer):
    oplock(false),
    m_hintsig(*this),
    m_statesig(*this),
    m_layersig(*this),
    m_workspacesig(*this),
    m_diesig(*this),
    moving(false), resizing(false), shaded(false), maximized(false),
    iconic(false), focused(false),
    stuck(false), send_focus_message(false), m_managed(false),
    screen(scr),
    timer(this),
    display(0),
    lastButtonPressTime(0),
    m_windowmenu(menutheme, scr.getScreenNumber(), *scr.getImageControl()),
    m_layermenu(menutheme, 
                scr.getScreenNumber(), 
                *scr.getImageControl(),
                *scr.layerManager().getLayer(Fluxbox::instance()->getMenuLayer()), 
                this,
                false),
    old_decoration(DECOR_NORMAL),
    m_client(new WinClient(w, *this)),
    m_frame(tm, *scr.getImageControl(), scr.getScreenNumber(), 0, 0, 100, 100),
    m_layeritem(m_frame.window(), layer),
    m_layernum(layer.getLayerNum()) {
    assert(w != 0);
    init();

}



FluxboxWindow::~FluxboxWindow() {
#ifdef DEBUG
    cerr<<__FILE__<<"("<<__LINE__<<"): starting ~FluxboxWindow("<<this<<")"<<endl;
#endif // DEBUG
    if (moving || resizing || m_attaching_tab) {
        screen.hideGeometry();
        XUngrabPointer(display, CurrentTime);
    }

    Client2ButtonMap::iterator it = m_labelbuttons.begin();
    Client2ButtonMap::iterator it_end = m_labelbuttons.end();
    for (; it != it_end; ++it) {
        m_frame.removeLabelButton(*(*it).second);
        delete (*it).second;
    }
    m_labelbuttons.clear();

    timer.stop();
    
    // notify die
    m_diesig.notify();

    if (m_client != 0)
        delete m_client; // this also removes client from our list
    m_client = 0;

    if (m_clientlist.size() > 1) {
        cerr<<__FILE__<<"("<<__FUNCTION__<<") WARNING! clientlist > 1"<<endl;
        while (!m_clientlist.empty()) {
            detachClient(*m_clientlist.back());
        }
    }
    Fluxbox::instance()->removeWindowSearch(m_frame.window().window());
#ifdef DEBUG
    cerr<<__FILE__<<"("<<__LINE__<<"): ~FluxboxWindow("<<this<<")"<<endl;
#endif // DEBUG
}


void FluxboxWindow::init() { 
    m_attaching_tab = 0;
    assert(m_client);
    //!! TODO init of client should be better
    // we don't want to duplicate code here and in attachClient
    m_clientlist.push_back(m_client);
#ifdef DEBUG
    cerr<<__FILE__<<": FluxboxWindow::init(this="<<this<<", client="<<hex<<
        m_client->window()<<", frame = "<<m_frame.window().window()<<dec<<")"<<endl;

#endif // DEBUG    

    m_frame.resize(m_client->width(), m_client->height());
    TextButton *btn =  new TextButton(m_frame.label(), 
                                      m_frame.theme().font(),
                                      m_client->title());
    btn->setJustify(m_frame.theme().justify());
    m_labelbuttons[m_client] = btn;
    m_frame.addLabelButton(*btn);
    m_frame.setLabelButtonFocus(*btn);
    btn->show();    
    FbTk::EventManager &evm = *FbTk::EventManager::instance();
    // we need motion notify so we mask it
    btn->window().setEventMask(ExposureMask | ButtonPressMask | ButtonReleaseMask | 
                               ButtonMotionMask);

    FbTk::RefCount<FbTk::Command> set_client_cmd(new SetClientCmd(*m_client));
    btn->setOnClick(set_client_cmd);
    evm.add(*this, btn->window()); // we take care of button events for this
    evm.add(*this, m_client->window());

    //    m_frame.reconfigure();

    // redirect events from frame to us

    m_frame.setEventHandler(*this); 

    lastFocusTime.tv_sec = lastFocusTime.tv_usec = 0;

    // display connection
    display = FbTk::App::instance()->display();

    blackbox_attrib.workspace = workspace_number = move_ws = window_number = -1;

    blackbox_attrib.flags = blackbox_attrib.attrib = blackbox_attrib.stack = 0;
    blackbox_attrib.premax_x = blackbox_attrib.premax_y = 0;
    blackbox_attrib.premax_w = blackbox_attrib.premax_h = 0;

    //use tab as default
    decorations.tab = true;
    // enable decorations
    decorations.enabled = true;

    // set default values for decoration
    decorations.menu = true;	//override menu option
    // all decorations on by default
    decorations.titlebar = decorations.border = decorations.handle = true;
    decorations.maximize = decorations.close = 
        decorations.sticky = decorations.shade = decorations.tab = true;


    functions.resize = functions.move = functions.iconify = functions.maximize = true;
    functions.close = decorations.close = false;

    getBlackboxHints();
    if (! m_client->blackbox_hint)
        getMWMHints();
    
    // get size, aspect, minimum/maximum size and other hints set
    // by the client

    getWMProtocols();
    getWMHints(); 
    getWMNormalHints();

    //!!
    // fetch client size and placement
    XWindowAttributes wattrib;
    if (! m_client->getAttrib(wattrib) ||
        !wattrib.screen // no screen? ??
        || wattrib.override_redirect) { // override redirect
        return;
    }

    // save old border width so we can restore it later
    m_client->old_bw = wattrib.border_width;
    m_client->x = wattrib.x; m_client->y = wattrib.y;

    Fluxbox *fluxbox = Fluxbox::instance();

    fluxbox->saveWindowSearch(m_frame.window().window(), this);

    timer.setTimeout(fluxbox->getAutoRaiseDelay());
    timer.fireOnce(true);

    if (m_client->initial_state == WithdrawnState) {
        return;
    }

    m_managed = true; //this window is managed
	
    // update transient infomation
    m_client->updateTransientInfo();
	
    // adjust the window decorations based on transience and window sizes
    if (m_client->isTransient()) {
        decorations.maximize =  functions.maximize = false;
        decorations.handle = decorations.border = false;
    }	
	
    if ((m_client->normal_hint_flags & PMinSize) &&
        (m_client->normal_hint_flags & PMaxSize) &&
        m_client->max_width != 0 && m_client->max_width <= m_client->min_width &&
        m_client->max_height != 0 && m_client->max_height <= m_client->min_height) {
        decorations.maximize = decorations.handle =
            functions.resize = functions.maximize = false;
        decorations.tab = false; //no tab for this window
    }

    upsize();

    bool place_window = true;
    if (fluxbox->isStartup() || m_client->isTransient() ||
        m_client->normal_hint_flags & (PPosition|USPosition)) {
        setGravityOffsets();

        if (! fluxbox->isStartup()) {

            int real_x = m_frame.x();
            int real_y = m_frame.y();

            if (real_x >= 0 && 
                real_y + m_frame.y() >= 0 &&
                real_x <= (signed) screen.getWidth() &&
                real_y <= (signed) screen.getHeight())
                place_window = false;

        } else
            place_window = false;

    }

    associateClientWindow();

    grabButtons();
		
    positionWindows();

    if (workspace_number < 0 || workspace_number >= screen.getCount())
        workspace_number = screen.getCurrentWorkspaceID();

    restoreAttributes();

    m_frame.move(wattrib.x, wattrib.y);
    m_frame.resizeForClient(wattrib.width, wattrib.height);

    // if we're a transient then we should be on the same layer as our parent
    if (m_client->isTransient() && 
        m_client->transientFor()->fbwindow() &&
        m_client->transientFor()->fbwindow() != this)
        getLayerItem().setLayer(m_client->transientFor()->fbwindow()->getLayerItem().getLayer());       
    else // if no parent then set default layer
        moveToLayer(m_layernum);
    
    if (!place_window)
        moveResize(m_frame.x(), m_frame.y(), m_frame.width(), m_frame.height());

    screen.getWorkspace(workspace_number)->addWindow(*this, place_window);

    if (shaded) { // start shaded
        shaded = false;
        shade();
    }

    if (maximized && functions.maximize) { // start maximized
        maximized = false;
        maximize();
    }	

    if (stuck) {
        stuck = false;
        stick();
        deiconify(); //we're omnipresent and visible
    }

    setState(current_state);
    m_frame.reconfigure();
    sendConfigureNotify();
    // no focus default
    setFocusFlag(false);

}

/// attach a client to this window and destroy old window
void FluxboxWindow::attachClient(WinClient &client) {
    //!! TODO: check for isGroupable in client
    if (client.m_win == this)
        return;

    // reparent client win to this frame 
    m_frame.setClientWindow(client);
    FbTk::EventManager &evm = *FbTk::EventManager::instance();

    if (client.fbwindow() != 0) {
        FluxboxWindow *old_win = client.fbwindow(); // store old window

        Fluxbox *fb = Fluxbox::instance();
        // make sure we set new window search for each client
        ClientList::iterator client_it = old_win->clientList().begin();
        ClientList::iterator client_it_end = old_win->clientList().end();
        for (; client_it != client_it_end; ++client_it) {
            fb->saveWindowSearch((*client_it)->window(), this);
            evm.add(*this, (*client_it)->window());

            // reparent window to this
            m_frame.setClientWindow(**client_it);
            resizeClient(**client_it, 
                         m_frame.clientArea().width(),
                         m_frame.clientArea().height());

            (*client_it)->m_win = this;
            // create a labelbutton for this client and 
            // associate it with the pointer
            TextButton *btn = new TextButton(m_frame.label(), 
                                             m_frame.theme().font(),
                                             (*client_it)->title());
            btn->setJustify(m_frame.theme().justify());
            m_labelbuttons[(*client_it)] = btn;
            m_frame.addLabelButton(*btn);
            btn->show();
            // we need motion notify so we mask it
            btn->window().setEventMask(ExposureMask | ButtonPressMask | 
                                       ButtonReleaseMask | ButtonMotionMask);


            FbTk::RefCount<FbTk::Command> 
                set_client_cmd(new SetClientCmd(*(*client_it)));
            btn->setOnClick(set_client_cmd);
            evm.add(*this, btn->window()); // we take care of button events for this

        }

        // add client and move over all attached clients 
        // from the old window to this list
        m_clientlist.splice(m_clientlist.end(), old_win->m_clientlist);           
        
        old_win->m_client = 0;
        delete old_win;
        
    } else {
        // create a labelbutton for this client and associate it with the pointer
        TextButton *btn = new TextButton(m_frame.label(), 
                                         m_frame.theme().font(),
                                         client.title());
        m_labelbuttons[&client] = btn;
        m_frame.addLabelButton(*btn);
        btn->show();
        FbTk::EventManager &evm = *FbTk::EventManager::instance();
        // we need motion notify so we mask it
        btn->window().setEventMask(ExposureMask | ButtonPressMask | 
                                   ButtonReleaseMask | ButtonMotionMask);


        FbTk::RefCount<FbTk::Command> set_client_cmd(new SetClientCmd(client));
        btn->setOnClick(set_client_cmd);
        evm.add(*this, btn->window()); // we take care of button events for this

        client.m_win = this;    

        Fluxbox::instance()->saveWindowSearch(client.window(), this);
    }

    m_frame.reconfigure();

    // keep the current window on top
    m_client->raise();



}


/// detach client from window and create a new window for it
bool FluxboxWindow::detachClient(WinClient &client) {
    if (client.m_win != this || numClients() <= 1)
        return false;
    
    removeClient(client);

    client.m_win = screen.createWindow(client);
    m_client->raise();
    setInputFocus();
    return true;
}

void FluxboxWindow::detachCurrentClient() {
    // should only operate if we had more than one client
    if (numClients() <= 1)
        return;
    detachClient(*m_client);
}

/// removes client from client list, does not create new fluxboxwindow for it
bool FluxboxWindow::removeClient(WinClient &client) {
    if (client.m_win != this || numClients() == 0)
        return false;

#ifdef DEBUG
    cerr<<__FILE__<<"("<<__FUNCTION__<<")["<<this<<"]"<<endl;
#endif // DEBUG
    
    // if it is our active client, deal with it...
    if (m_client == &client) {
    // set next client to be focused
    // if the client we're about to remove is the last client then set prev client
        if (&client == m_clientlist.back())
            prevClient();
        else
            nextClient();
    }

    client.m_win = 0;
    m_clientlist.remove(&client);

    if (m_client == &client) {
        if (m_clientlist.empty())
            m_client = 0;
        else
            // this really shouldn't happen
            m_client = m_clientlist.back();
    }

    FbTk::EventManager &evm = *FbTk::EventManager::instance();
    evm.remove(client.window());

    FbTk::Button *label_btn = m_labelbuttons[&client];
    if (label_btn != 0) {
        m_frame.removeLabelButton(*label_btn);
        evm.remove(label_btn->window());
        delete label_btn;
        label_btn = 0;
    }

    m_labelbuttons.erase(&client);

#ifdef DEBUG
    cerr<<__FILE__<<"("<<__FUNCTION__<<")["<<this<<"] numClients = "<<numClients()<<endl;
#endif // DEBUG   

    return true;
}

/// returns WinClient of window we're searching for
WinClient *FluxboxWindow::findClient(Window win) {
    std::list<WinClient *>::iterator it = m_clientlist.begin();
    std::list<WinClient *>::iterator it_end = m_clientlist.end();
    for (; it != it_end; ++it) {
        if ((*it)->window() == win)
            return (*it);
    }
    // failure
    return 0;
}

/// raise and focus next client
void FluxboxWindow::nextClient() {
    if (numClients() <= 1)
        return;

    ClientList::iterator it = find(m_clientlist.begin(), m_clientlist.end(), m_client);
    if (it == m_clientlist.end()) {
        m_client = m_clientlist.front();
        return;
    }

    it++;
    if (it == m_clientlist.end())
        m_client = m_clientlist.front();
    else
        m_client = *it;
    m_client->raise();
    m_frame.setLabelButtonFocus(*m_labelbuttons[m_client]);
    setInputFocus();
}

void FluxboxWindow::prevClient() {
    if (numClients() <= 1)
        return;

    ClientList::iterator it = find(m_clientlist.begin(), m_clientlist.end(), m_client);
    if (it == m_clientlist.end()) {
        m_client = m_clientlist.front();
        return;
    }
    if (it == m_clientlist.begin())
        m_client = m_clientlist.back();
    else
        m_client = *(--it);

    m_client->raise();
    m_frame.setLabelButtonFocus(*m_labelbuttons[m_client]);
    setInputFocus();
}

bool FluxboxWindow::setCurrentClient(WinClient &client, bool setinput) {
    // make sure it's in our list
    if (client.m_win != this)
        return false;

    m_client = &client;
    m_client->raise();
    m_frame.setLabelButtonFocus(*m_labelbuttons[m_client]);
    return setinput && setInputFocus();
}

bool FluxboxWindow::isGroupable() const {
    if (isResizable() && isMaximizable() && !winClient().isTransient())
        return true;
    return false;
}

void FluxboxWindow::associateClientWindow() {
    m_client->setBorderWidth(0);
    updateTitleFromClient();
    updateIconNameFromClient();

    m_frame.setClientWindow(*m_client);
    m_frame.resizeForClient(m_client->width(), m_client->height());
    // make sure the frame reconfigures
    m_frame.reconfigure();
}


void FluxboxWindow::grabButtons() {
    Fluxbox *fluxbox = Fluxbox::instance();

    XGrabButton(display, Button1, AnyModifier, 
		m_frame.clientArea().window(), True, ButtonPressMask,
		GrabModeSync, GrabModeSync, None, None);		
    XUngrabButton(display, Button1, Mod1Mask|Mod2Mask|Mod3Mask, m_frame.clientArea().window());


    XGrabButton(display, Button1, Mod1Mask, m_frame.window().window(), True,
		ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
		GrabModeAsync, None, fluxbox->getMoveCursor());

    //----grab with "all" modifiers
    grabButton(display, Button1, m_frame.window().window(), fluxbox->getMoveCursor());
	
    XGrabButton(display, Button2, Mod1Mask, m_frame.window().window(), True,
		ButtonReleaseMask, GrabModeAsync, GrabModeAsync, None, None);
		
    XGrabButton(display, Button3, Mod1Mask, m_frame.window().window(), True,
		ButtonReleaseMask | ButtonMotionMask, GrabModeAsync,
		GrabModeAsync, None, fluxbox->getLowerRightAngleCursor());
	
    //---grab with "all" modifiers
    grabButton(display, Button3, m_frame.window().window(), fluxbox->getLowerRightAngleCursor());
}


void FluxboxWindow::reconfigure() {
    
    upsize();

    positionWindows();

    setFocusFlag(focused);

    moveResize(m_frame.x(), m_frame.y(), m_frame.width(), m_frame.height());
	
    grabButtons();

    m_frame.setDoubleClickTime(Fluxbox::instance()->getDoubleClickInterval());

    m_windowmenu.reconfigure();
	
}


void FluxboxWindow::positionWindows() {

    m_frame.window().setBorderWidth(screen.rootTheme().borderWidth());
    m_frame.clientArea().setBorderWidth(0); // client area bordered by other things

    m_frame.titlebar().setBorderWidth(screen.rootTheme().borderWidth());
    if (decorations.titlebar) {
        m_frame.showTitlebar();
    } else {
        m_frame.hideTitlebar();
    }
    
    m_frame.handle().setBorderWidth(screen.rootTheme().borderWidth());
    m_frame.gripLeft().setBorderWidth(screen.rootTheme().borderWidth());
    m_frame.gripRight().setBorderWidth(screen.rootTheme().borderWidth());

    if (decorations.handle)
        m_frame.showHandle();
    else 
        m_frame.hideHandle();
	
    m_frame.reconfigure();

}

/// update current client title and title in our frame
void FluxboxWindow::updateTitleFromClient() {

    m_client->updateTitle();
    m_labelbuttons[m_client]->setText(m_client->title());    
    m_labelbuttons[m_client]->clear(); // redraw text
}

/// update icon title from client
void FluxboxWindow::updateIconNameFromClient() {
    m_client->updateIconTitle();
}


void FluxboxWindow::getWMProtocols() {
    Atom *proto = 0;
    int num_return = 0;
    FbAtoms *fbatoms = FbAtoms::instance();

    if (XGetWMProtocols(display, m_client->window(), &proto, &num_return)) {

        for (int i = 0; i < num_return; ++i) {
            if (proto[i] == fbatoms->getWMDeleteAtom())
                functions.close = true;
            else if (proto[i] == fbatoms->getWMTakeFocusAtom())
                send_focus_message = true;
            else if (proto[i] == fbatoms->getFluxboxStructureMessagesAtom())
                screen.addNetizen(new Netizen(&screen, m_client->window()));
        }

        XFree(proto);
    } else {
        cerr<<"Warning: Failed to read WM Protocols. "<<endl;
    }

}


void FluxboxWindow::getWMHints() {
    //!!
    XWMHints *wmhint = XGetWMHints(display, m_client->window());
    if (! wmhint) {
        iconic = false;
        focus_mode = F_PASSIVE;
        m_client->window_group = None;
        m_client->initial_state = NormalState;
    } else {
        m_client->wm_hint_flags = wmhint->flags;
        if (wmhint->flags & InputHint) {
            if (wmhint->input) {
                if (send_focus_message)
                    focus_mode = F_LOCALLYACTIVE;
                else
                    focus_mode = F_PASSIVE;
            } else {
                if (send_focus_message)
                    focus_mode = F_GLOBALLYACTIVE;
                else
                    focus_mode = F_NOINPUT;
            }
        } else
            focus_mode = F_PASSIVE;

        if (wmhint->flags & StateHint)
            m_client->initial_state = wmhint->initial_state;
        else
            m_client->initial_state = NormalState;

        if (wmhint->flags & WindowGroupHint) {
            if (! m_client->window_group) {
                m_client->window_group = wmhint->window_group;
                Fluxbox::instance()->saveGroupSearch(m_client->window_group, this);
            }
        } else
            m_client->window_group = None;

        XFree(wmhint);
    }
}


void FluxboxWindow::getWMNormalHints() {
    long icccm_mask;
    XSizeHints sizehint;
    if (! XGetWMNormalHints(display, m_client->window(), &sizehint, &icccm_mask)) {
        m_client->min_width = m_client->min_height =
            m_client->base_width = m_client->base_height =
            m_client->width_inc = m_client->height_inc = 1;
        m_client->max_width = 0; // unbounded
        m_client->max_height = 0;
        m_client->min_aspect_x = m_client->min_aspect_y =
            m_client->max_aspect_x = m_client->max_aspect_y = 1;
        m_client->win_gravity = NorthWestGravity;
    } else {
        m_client->normal_hint_flags = sizehint.flags;

        if (sizehint.flags & PMinSize) {
            m_client->min_width = sizehint.min_width;
            m_client->min_height = sizehint.min_height;
        } else
            m_client->min_width = m_client->min_height = 1;

        if (sizehint.flags & PMaxSize) {
            m_client->max_width = sizehint.max_width;
            m_client->max_height = sizehint.max_height;
        } else {
            m_client->max_width = 0; // unbounded
            m_client->max_height = 0;
        }

        if (sizehint.flags & PResizeInc) {
            m_client->width_inc = sizehint.width_inc;
            m_client->height_inc = sizehint.height_inc;
        } else
            m_client->width_inc = m_client->height_inc = 1;

        if (sizehint.flags & PAspect) {
            m_client->min_aspect_x = sizehint.min_aspect.x;
            m_client->min_aspect_y = sizehint.min_aspect.y;
            m_client->max_aspect_x = sizehint.max_aspect.x;
            m_client->max_aspect_y = sizehint.max_aspect.y;
        } else
            m_client->min_aspect_x = m_client->min_aspect_y =
                m_client->max_aspect_x = m_client->max_aspect_y = 1;

        if (sizehint.flags & PBaseSize) {
            m_client->base_width = sizehint.base_width;
            m_client->base_height = sizehint.base_height;
        } else
            m_client->base_width = m_client->base_height = 0;

        if (sizehint.flags & PWinGravity)
            m_client->win_gravity = sizehint.win_gravity;
        else
            m_client->win_gravity = NorthWestGravity;
    }
}


void FluxboxWindow::getMWMHints() {
    int format;
    Atom atom_return;
    unsigned long num, len;
    Atom  motif_wm_hints = XInternAtom(display, "_MOTIF_WM_HINTS", False);
    if (!XGetWindowProperty(display, m_client->window(),
                            motif_wm_hints, 0,
                            PropMwmHintsElements, false,
                            motif_wm_hints, &atom_return,
                            &format, &num, &len,
                            (unsigned char **) &m_client->mwm_hint) == Success &&
        m_client->mwm_hint) {
        return;
    }
    if (num != PropMwmHintsElements)
        return;
	
    if (m_client->mwm_hint->flags & MwmHintsDecorations) {
        if (m_client->mwm_hint->decorations & MwmDecorAll) {
            decorations.titlebar = decorations.handle = decorations.border =
                decorations.iconify = decorations.maximize =
                decorations.close = decorations.menu = true;
        } else {
            decorations.titlebar = decorations.handle = decorations.border =
                decorations.iconify = decorations.maximize =
                decorations.close = decorations.tab = false;
            decorations.menu = true;
            if (m_client->mwm_hint->decorations & MwmDecorBorder)
                decorations.border = true;
            if (m_client->mwm_hint->decorations & MwmDecorHandle)
                decorations.handle = true;
            if (m_client->mwm_hint->decorations & MwmDecorTitle) {                
                //only tab on windows with titlebar
                decorations.titlebar = decorations.tab = true;
            }
            
            if (m_client->mwm_hint->decorations & MwmDecorMenu)
                decorations.menu = true;
            if (m_client->mwm_hint->decorations & MwmDecorIconify)
                decorations.iconify = true;
            if (m_client->mwm_hint->decorations & MwmDecorMaximize)
                decorations.maximize = true;
        }
    }
	
    if (m_client->mwm_hint->flags & MwmHintsFunctions) {
        if (m_client->mwm_hint->functions & MwmFuncAll) {
            functions.resize = functions.move = functions.iconify =
                functions.maximize = functions.close = true;
        } else {
            functions.resize = functions.move = functions.iconify =
                functions.maximize = functions.close = false;

            if (m_client->mwm_hint->functions & MwmFuncResize)
                functions.resize = true;
            if (m_client->mwm_hint->functions & MwmFuncMove)
                functions.move = true;
            if (m_client->mwm_hint->functions & MwmFuncIconify)
                functions.iconify = true;
            if (m_client->mwm_hint->functions & MwmFuncMaximize)
                functions.maximize = true;
            if (m_client->mwm_hint->functions & MwmFuncClose)
                functions.close = true;
        }
    }
	
	
}


void FluxboxWindow::getBlackboxHints() {
    int format;
    Atom atom_return;
    unsigned long num, len;
    FbAtoms *atoms = FbAtoms::instance();
	
    if (XGetWindowProperty(display, m_client->window(),
                           atoms->getFluxboxHintsAtom(), 0,
                           PropBlackboxHintsElements, False,
                           atoms->getFluxboxHintsAtom(), &atom_return,
                           &format, &num, &len,
                           (unsigned char **) &m_client->blackbox_hint) == Success &&
        m_client->blackbox_hint) {

        if (num == PropBlackboxHintsElements) {
            if (m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_SHADED)
                shaded = (m_client->blackbox_hint->attrib & BaseDisplay::ATTRIB_SHADED);

            if ((m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_MAXHORIZ) &&
                (m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_MAXVERT))
                maximized = ((m_client->blackbox_hint->attrib &
                              (BaseDisplay::ATTRIB_MAXHORIZ | 
                               BaseDisplay::ATTRIB_MAXVERT)) ? 1 : 0);
            else if (m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_MAXVERT)
                maximized = ((m_client->blackbox_hint->attrib & 
                              BaseDisplay::ATTRIB_MAXVERT) ? 2 : 0);
            else if (m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_MAXHORIZ)
                maximized = ((m_client->blackbox_hint->attrib & 
                              BaseDisplay::ATTRIB_MAXHORIZ) ? 3 : 0);

            if (m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_OMNIPRESENT)
                stuck = (m_client->blackbox_hint->attrib & 
                         BaseDisplay::ATTRIB_OMNIPRESENT);

            if (m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_WORKSPACE)
                workspace_number = m_client->blackbox_hint->workspace;

            if (m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_STACK)
                workspace_number = m_client->blackbox_hint->stack;

            if (m_client->blackbox_hint->flags & BaseDisplay::ATTRIB_DECORATION) {
                old_decoration = static_cast<Decoration>(m_client->blackbox_hint->decoration);
                setDecoration(old_decoration);
            }
        }
    }
}

void FluxboxWindow::move(int x, int y) {
    moveResize(x, y, m_frame.width(), m_frame.height());
}

void FluxboxWindow::resize(unsigned int width, unsigned int height) {
    moveResize(m_frame.x(), m_frame.y(), width, height);
}

void FluxboxWindow::moveResize(int new_x, int new_y,
                               unsigned int new_width, unsigned int new_height) {

    bool send_event = (m_frame.x() != new_x || m_frame.y() != new_y);

    if (new_width != m_frame.width() || new_height != m_frame.height()) {
        if ((((signed) m_frame.width()) + new_x) < 0) 
            new_x = 0;
        if ((((signed) m_frame.height()) + new_y) < 0) 
            new_y = 0;

        downsize();

        m_frame.moveResize(new_x, new_y, new_width, new_height);

        setFocusFlag(focused);
        shaded = false;
        send_event = true;
    } else {
        m_frame.move(new_x, new_y);
		
        send_event = true;
    }

    if (send_event && ! moving) {
        sendConfigureNotify();
    }
}	

bool FluxboxWindow::setInputFocus() {

    //TODO hint skip focus
    if (((signed) (m_frame.x() + m_frame.width())) < 0) {
        if (((signed) (m_frame.y() + m_frame.height())) < 0) {
            moveResize(screen.rootTheme().borderWidth(), screen.rootTheme().borderWidth(),
                       m_frame.width(), m_frame.height());
        } else if (m_frame.y() > (signed) screen.getHeight()) {
            moveResize(screen.rootTheme().borderWidth(), screen.getHeight() - m_frame.height(),
                       m_frame.width(), m_frame.height());
        } else {
            moveResize(screen.rootTheme().borderWidth(), m_frame.y() + screen.rootTheme().borderWidth(),
                       m_frame.width(), m_frame.height());
        }
    } else if (m_frame.x() > (signed) screen.getWidth()) {
        if (((signed) (m_frame.y() + m_frame.height())) < 0) {
            moveResize(screen.getWidth() - m_frame.width(), screen.rootTheme().borderWidth(),
                       m_frame.width(), m_frame.height());
        } else if (m_frame.y() > (signed) screen.getHeight()) {
            moveResize(screen.getWidth() - m_frame.width(),
                       screen.getHeight() - m_frame.height(), 
                       m_frame.width(), m_frame.height());
        } else {
            moveResize(screen.getWidth() - m_frame.width(),
                       m_frame.y() + screen.rootTheme().borderWidth(), 
                       m_frame.width(), m_frame.height());
        }
    }

    if (! validateClient())
        return false;

    bool ret = false;

    if (!m_client->transients.empty() && m_client->isModal()) {
        WinClient::TransientList::iterator it = m_client->transients.begin();
        WinClient::TransientList::iterator it_end = m_client->transients.end();
        for (; it != it_end; ++it) {
            if ((*it)->isModal())
                return (*it)->fbwindow()->setCurrentClient(**it,true);
        }
    } else {
        if (focus_mode == F_LOCALLYACTIVE || focus_mode == F_PASSIVE) {
            XSetInputFocus(display, m_client->window(),
                           RevertToPointerRoot, CurrentTime);
        } else {
            return false;
        }

	screen.setFocusedWindow(*m_client);

        Fluxbox::instance()->setFocusedWindow(this);

        if (send_focus_message)
            m_client->sendFocus();

        if ((screen.isSloppyFocus() || screen.isSemiSloppyFocus())
            && screen.doAutoRaise())
            timer.start();

        ret = true;
    }

    return ret;
}

void FluxboxWindow::hide() {
#ifdef DEBUG
    cerr<<__FILE__<<"("<<__FUNCTION__<<")["<<this<<"]"<<endl;
#endif // DEBUG
    m_windowmenu.hide();
    m_frame.hide();
}

void FluxboxWindow::show() {
    m_frame.show();
}

/**
   Unmaps the window and removes it from workspace list
*/
void FluxboxWindow::iconify() {
    if (isIconic()) // no need to iconify if we're already
        return;

    m_windowmenu.hide();
    iconic = true;

    setState(IconicState);

    m_frame.hide();

    ClientList::iterator client_it = m_clientlist.begin();
    const ClientList::iterator client_it_end = m_clientlist.end();
    for (; client_it != client_it_end; ++client_it) {
        WinClient &client = *(*client_it);
        client.setEventMask(NoEventMask);
        client.hide();
        client.setEventMask(PropertyChangeMask | StructureNotifyMask | FocusChangeMask);
        if (client.transientFor() &&
            client.transientFor()->fbwindow()) {
            if (!client.transientFor()->fbwindow()->isIconic()) {
                client.transientFor()->fbwindow()->iconify();
            }
        }

        if (!client.transientList().empty()) {
            WinClient::TransientList::iterator it = client.transientList().begin();
            WinClient::TransientList::iterator it_end = client.transientList().end();
            for (; it != it_end; it++)
                if ((*it)->fbwindow()) 
                    (*it)->fbwindow()->iconify();
        }
    }

}

void FluxboxWindow::deiconify(bool reassoc, bool do_raise) {
    if (oplock) return;
    oplock = true;

    if (iconic || reassoc) {
        screen.reassociateWindow(this, screen.getCurrentWorkspace()->workspaceID(), false);
    } else if (moving || workspace_number != screen.getCurrentWorkspace()->workspaceID())
        return;

    bool was_iconic = iconic;

    iconic = false;
    setState(NormalState);

    ClientList::iterator client_it = clientList().begin();
    ClientList::iterator client_it_end = clientList().end();
    for (; client_it != client_it_end; ++client_it) {
        (*client_it)->setEventMask(NoEventMask);        
        (*client_it)->show();
        (*client_it)->setEventMask(PropertyChangeMask | StructureNotifyMask | FocusChangeMask);
    }
    
    m_frame.show();

    if (was_iconic && screen.doFocusNew())
        setInputFocus();

    if (focused != m_frame.focused())
        m_frame.setFocus(focused);


    if (reassoc && !m_client->transients.empty()) {
        // deiconify all transients
        client_it = clientList().begin();
        for (; client_it != client_it_end; ++client_it) {
            //TODO: Can this get stuck in a loop?
            WinClient::TransientList::iterator trans_it = 
                (*client_it)->transientList().begin();
            WinClient::TransientList::iterator trans_it_end = 
                (*client_it)->transientList().end();
            for (; trans_it != trans_it_end; ++trans_it) {
                if ((*trans_it)->fbwindow())
                    (*trans_it)->fbwindow()->deiconify(true, false);
            }
        }
    }
    oplock = false;
    if (do_raise)
	raise();
}

/**
   Send close request to client window
*/
void FluxboxWindow::close() {
#ifdef DEBUG
    cerr<<__FILE__<<"("<<__FUNCTION__<<")"<<endl;
#endif // DEBUG    
    m_client->sendClose();
}

/**
 Set window in withdrawn state
*/
void FluxboxWindow::withdraw() {
    iconic = false;

    if (isResizing())
        stopResizing();

    m_frame.hide();

    m_windowmenu.hide();
}

/**
   Maximize window both horizontal and vertical
*/
void FluxboxWindow::maximize() {
    if (isIconic())
        deiconify();

    if (!maximized) {
        // save old values
        m_old_width = frame().width();
        m_old_height = frame().height();
        m_old_pos_x = frame().x();
        m_old_pos_y = frame().y();
        unsigned int left_x = screen.getMaxLeft();
        unsigned int max_width = screen.getMaxRight();
        unsigned int max_top = screen.getMaxTop();
        moveResize(left_x, max_top, 
                   max_width - left_x, 
                   screen.getMaxBottom() - max_top - m_frame.window().borderWidth());
    } else { // demaximize, restore to old values
        moveResize(m_old_pos_x, m_old_pos_y,
                   m_old_width, m_old_height);
    }
    // toggle maximize
    maximized = !maximized;
}

void FluxboxWindow::maximizeHorizontal() {
    unsigned int left_x = screen.getMaxLeft();
    unsigned int max_width = screen.getMaxRight();
    moveResize(left_x, m_frame.y(), 
               max_width - left_x, m_frame.height() - m_frame.window().borderWidth());

}

/**
 Maximize window horizontal
 */
void FluxboxWindow::maximizeVertical() {
    unsigned int max_top = screen.getMaxTop();
    moveResize(m_frame.x(), max_top,
               m_frame.width() - m_frame.window().borderWidth(), 
               screen.getMaxBottom() - max_top);
}


void FluxboxWindow::setWorkspace(int n) {

    workspace_number = n;

    blackbox_attrib.flags |= BaseDisplay::ATTRIB_WORKSPACE;
    blackbox_attrib.workspace = workspace_number;

    // notify workspace change
#ifdef DEBUG
    cerr<<this<<" notify workspace signal"<<endl;
#endif // DEBUG
    m_workspacesig.notify();
}

void FluxboxWindow::setLayerNum(int layernum) {
    m_layernum = layernum;

    blackbox_attrib.flags |= BaseDisplay::ATTRIB_STACK;
    blackbox_attrib.stack = layernum;
    saveBlackboxHints();

#ifdef DEBUG
    cerr<<this<<" notify layer signal"<<endl;
#endif // DEBUG

    m_layersig.notify();
}

void FluxboxWindow::shade() {
    // we can only shade if we have a titlebar
    if (!decorations.titlebar)
        return;

    m_frame.shade();

    if (shaded) {
        shaded = false;
        blackbox_attrib.flags ^= BaseDisplay::ATTRIB_SHADED;
        blackbox_attrib.attrib ^= BaseDisplay::ATTRIB_SHADED;

        setState(NormalState);
    } else {
        shaded = true;
        blackbox_attrib.flags |= BaseDisplay::ATTRIB_SHADED;
        blackbox_attrib.attrib |= BaseDisplay::ATTRIB_SHADED;
        // shading is the same as iconic
        setState(IconicState);
    }

}


void FluxboxWindow::stick() {

    if (stuck) {
        blackbox_attrib.flags ^= BaseDisplay::ATTRIB_OMNIPRESENT;
        blackbox_attrib.attrib ^= BaseDisplay::ATTRIB_OMNIPRESENT;

        stuck = false;

    } else {
        stuck = true;

        blackbox_attrib.flags |= BaseDisplay::ATTRIB_OMNIPRESENT;
        blackbox_attrib.attrib |= BaseDisplay::ATTRIB_OMNIPRESENT;

    }
 
    setState(current_state);
}


void FluxboxWindow::raise() {
    if (isIconic())
        deiconify();

    // get root window
    WinClient *client = getRootTransientFor(m_client);

    // if we don't have any root window use this as root
    if (client == 0) 
        client = m_client;

    // raise this window and every transient in it
    if (client->fbwindow())
        raiseFluxboxWindow(*client->fbwindow());
}

void FluxboxWindow::lower() {
    if (isIconic())
        deiconify();

    // get root window
    WinClient *client = getRootTransientFor(m_client);
    
    // if we don't have any root window use this as root
    if (client == 0) 
        client = m_client;

    if (client->fbwindow())
        lowerFluxboxWindow(*client->fbwindow());
}

void FluxboxWindow::tempRaise() {
    if (isIconic())
        deiconify();

    // get root window
    WinClient *client = getRootTransientFor(m_client);
    
    // if we don't have any root window use this as root
    if (client == 0) 
        client = m_client;

    if (client->fbwindow())
        tempRaiseFluxboxWindow(*client->fbwindow());
}


void FluxboxWindow::raiseLayer() {
    // don't let it up to menu layer
    if (getLayerNum() == (Fluxbox::instance()->getMenuLayer()+1))
        return;

    // get root window
    WinClient *client = getRootTransientFor(m_client);
    
    // if we don't have any root window use this as root
    if (client == 0) 
        client = m_client;

    FluxboxWindow *win = client->fbwindow();
    if (!win) return;

    if (!win->isIconic())
        screen.updateNetizenWindowRaise(client->window());

    win->getLayerItem().raiseLayer();

    // remember number just in case a transient happens to revisit this window
    int layer_num = win->getLayerItem().getLayerNum();
    win->setLayerNum(layer_num);

    WinClient::TransientList::const_iterator it = client->transientList().begin();
    WinClient::TransientList::const_iterator it_end = client->transientList().end();
    for (; it != it_end; ++it) {
        win = (*it)->fbwindow();
        if (win && !win->isIconic()) {
            screen.updateNetizenWindowRaise((*it)->window());
            win->getLayerItem().moveToLayer(layer_num);
            win->setLayerNum(layer_num);
        }
    }
}

void FluxboxWindow::lowerLayer() {
    // get root window
    WinClient *client = getRootTransientFor(m_client);
    
    // if we don't have any root window use this as root
    if (client == 0) 
        client = m_client;

    FluxboxWindow *win = client->fbwindow();
    if (!win) return;

    if (!win->isIconic()) {
        screen.updateNetizenWindowLower(client->window());
    }
    win->getLayerItem().lowerLayer();
    // remember number just in case a transient happens to revisit this window
    int layer_num = win->getLayerItem().getLayerNum();
    win->setLayerNum(layer_num);

    WinClient::TransientList::const_iterator it = client->transientList().begin();
    WinClient::TransientList::const_iterator it_end = client->transientList().end();
    for (; it != it_end; ++it) {
        win = (*it)->fbwindow();
        if (win && !win->isIconic()) {
            screen.updateNetizenWindowLower((*it)->window());
            win->getLayerItem().moveToLayer(layer_num);
            win->setLayerNum(layer_num);
        }
    }
}


void FluxboxWindow::moveToLayer(int layernum) {
    Fluxbox * fluxbox = Fluxbox::instance();

    // don't let it set its layer into menu area
    if (layernum <= fluxbox->getMenuLayer()) {
        layernum = fluxbox->getMenuLayer() + 1;
    }

    // get root window
    WinClient *client = getRootTransientFor(m_client);
    
    // if we don't have any root window use this as root
    if (client == 0) 
        client = m_client;

    FluxboxWindow *win = client->fbwindow();
    if (!win) return;

    if (!win->isIconic()) {
        screen.updateNetizenWindowRaise(client->window());
    }
    win->getLayerItem().lowerLayer();
    // remember number just in case a transient happens to revisit this window
    layernum = win->getLayerItem().getLayerNum();
    win->setLayerNum(layernum);

    WinClient::TransientList::const_iterator it = client->transientList().begin();
    WinClient::TransientList::const_iterator it_end = client->transientList().end();
    for (; it != it_end; ++it) {
        win = (*it)->fbwindow();
        if (win && !win->isIconic()) {
            screen.updateNetizenWindowRaise((*it)->window());
            win->getLayerItem().moveToLayer(layernum);
            win->setLayerNum(layernum);
        }
    }
}



void FluxboxWindow::setFocusFlag(bool focus) {
    focused = focus;

    // Record focus timestamp for window cycling enhancements
    if (focused)
        gettimeofday(&lastFocusTime, 0);

    m_frame.setFocus(focus);

    if ((screen.isSloppyFocus() || screen.isSemiSloppyFocus()) &&
        screen.doAutoRaise())
        timer.stop();
}


void FluxboxWindow::installColormap(bool install) {
    Fluxbox *fluxbox = Fluxbox::instance();
    fluxbox->grab();
    if (! validateClient()) return;

    int i = 0, ncmap = 0;
    Colormap *cmaps = XListInstalledColormaps(display, m_client->window(), &ncmap);
    XWindowAttributes wattrib;
    if (cmaps) { //!!
        if (m_client->getAttrib(wattrib)) {
            if (install) {
                // install the window's colormap
                for (i = 0; i < ncmap; i++) {
                    if (*(cmaps + i) == wattrib.colormap) {
                        // this window is using an installed color map... do not install
                        install = false;
                        break; //end for-loop (we dont need to check more)
                    }
                }
                // otherwise, install the window's colormap
                if (install)
                    XInstallColormap(display, wattrib.colormap);
            } else {				
                for (i = 0; i < ncmap; i++) { // uninstall the window's colormap
                    if (*(cmaps + i) == wattrib.colormap)
                       XUninstallColormap(display, wattrib.colormap);
                }
            }
        }

        XFree(cmaps);
    }

    fluxbox->ungrab();
}

/**
 Saves blackbox hints for every client in our list
 */
void FluxboxWindow::saveBlackboxHints() {
    for_each(m_clientlist.begin(), m_clientlist.end(), 
             FbTk::ChangeProperty(display, FbAtoms::instance()->getFluxboxAttributesAtom(),
                            PropModeReplace, 
                            (unsigned char *)&blackbox_attrib,
                            PropBlackboxAttributesElements));
}

/**
 Sets state on each client in our list
 */
void FluxboxWindow::setState(unsigned long new_state) {
    current_state = new_state;
    unsigned long state[2];
    state[0] = (unsigned long) current_state;
    state[1] = (unsigned long) None;
    for_each(m_clientlist.begin(), m_clientlist.end(),
             FbTk::ChangeProperty(display, FbAtoms::instance()->getWMStateAtom(),
                            PropModeReplace,
                            (unsigned char *)state, 2));

    saveBlackboxHints();
    //notify state changed
    m_statesig.notify();
}

bool FluxboxWindow::getState() {
    current_state = 0;

    Atom atom_return;
    bool ret = false;
    int foo;
    unsigned long *state, ulfoo, nitems;
    if ((XGetWindowProperty(display, m_client->window(), FbAtoms::instance()->getWMStateAtom(),
                            0l, 2l, false, FbAtoms::instance()->getWMStateAtom(),
                            &atom_return, &foo, &nitems, &ulfoo,
                            (unsigned char **) &state) != Success) ||
        (! state)) {
        return false;
    }

    if (nitems >= 1) {
        current_state = static_cast<unsigned long>(state[0]);
        ret = true;
    }

    XFree(static_cast<void *>(state));

    return ret;
}

//!! TODO: this functions looks odd
void FluxboxWindow::setGravityOffsets() {
    int newx = m_frame.x();
    int newy = m_frame.y();
    // translate x coordinate
    switch (m_client->win_gravity) {
        // handle Westward gravity
    case NorthWestGravity:
    case WestGravity:
    case SouthWestGravity:
    default:
#ifdef DEBUG
        cerr<<__FILE__<<": Default gravity: SouthWest, NorthWest, West"<<endl;
#endif // DEBUG

        newx = m_frame.x();
        break;

        // handle Eastward gravity
    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
#ifdef DEBUG
        cerr<<__FILE__<<": Gravity: SouthEast, NorthEast, East"<<endl;
#endif // DEBUG

        newx = m_frame.x() + m_frame.clientArea().width() - m_frame.width();
        break;

        // no x translation desired - default
    case StaticGravity:
    case ForgetGravity:
    case CenterGravity:
#ifdef DEBUG
        cerr<<__FILE__<<": Gravity: Center, Forget, Static"<<endl;
#endif // DEBUG

        newx = m_frame.x();
    }

    // translate y coordinate
    switch (m_client->win_gravity) {
        // handle Northbound gravity
    case NorthWestGravity:
    case NorthGravity:
    case NorthEastGravity:
    default:
        newy = m_frame.y();
        break;

        // handle Southbound gravity
    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
        newy = m_frame.y() + m_frame.clientArea().height() - m_frame.height();
        break;

        // no y translation desired - default
    case StaticGravity:
    case ForgetGravity:
    case CenterGravity:
        newy = m_frame.y();
        break;
    }
    // finaly move the frame
    if (m_frame.x() != newx || m_frame.y() != newy)
        m_frame.move(newx, newy);
}

/** 
 * Sets the attributes to what they should be
 * but doesn't change the actual state
 * (so the caller can set defaults etc as well)
 */
void FluxboxWindow::restoreAttributes() {
    if (!getState())
        current_state = NormalState;

    Atom atom_return;
    int foo;
    unsigned long ulfoo, nitems;
    FbAtoms *fbatoms = FbAtoms::instance();
	
    BaseDisplay::BlackboxAttributes *net;
    if (XGetWindowProperty(display, m_client->window(),
                           fbatoms->getFluxboxAttributesAtom(), 0l,
                           PropBlackboxAttributesElements, false,
                           fbatoms->getFluxboxAttributesAtom(), &atom_return, &foo,
                           &nitems, &ulfoo, (unsigned char **) &net) ==
        Success && net && nitems == PropBlackboxAttributesElements) {
        blackbox_attrib.flags = net->flags;
        blackbox_attrib.attrib = net->attrib;
        blackbox_attrib.workspace = net->workspace;
        blackbox_attrib.stack = net->stack;
        blackbox_attrib.premax_x = net->premax_x;
        blackbox_attrib.premax_y = net->premax_y;
        blackbox_attrib.premax_w = net->premax_w;
        blackbox_attrib.premax_h = net->premax_h;

        XFree(static_cast<void *>(net));
    } else
        return;

    if (blackbox_attrib.flags & BaseDisplay::ATTRIB_SHADED &&
        blackbox_attrib.attrib & BaseDisplay::ATTRIB_SHADED) {
        int save_state =
            ((current_state == IconicState) ? NormalState : current_state);

        shaded = true;
			
        current_state = save_state;
    }

    if (( blackbox_attrib.workspace != screen.getCurrentWorkspaceID()) &&
        ( blackbox_attrib.workspace < screen.getCount())) {
        workspace_number = blackbox_attrib.workspace;

        if (current_state == NormalState) current_state = WithdrawnState;
    } else if (current_state == WithdrawnState)
        current_state = NormalState;

    if (blackbox_attrib.flags & BaseDisplay::ATTRIB_OMNIPRESENT &&
        blackbox_attrib.attrib & BaseDisplay::ATTRIB_OMNIPRESENT) {
        stuck = true;

        current_state = NormalState;
    }

    if (blackbox_attrib.flags & BaseDisplay::ATTRIB_STACK) {
        //!! TODO check value?
        m_layernum = blackbox_attrib.stack;
    }

    if ((blackbox_attrib.flags & BaseDisplay::ATTRIB_MAXHORIZ) ||
        (blackbox_attrib.flags & BaseDisplay::ATTRIB_MAXVERT)) {
        int x = blackbox_attrib.premax_x, y = blackbox_attrib.premax_y;
        unsigned int w = blackbox_attrib.premax_w, h = blackbox_attrib.premax_h;
        maximized = false;
        if ((blackbox_attrib.flags & BaseDisplay::ATTRIB_MAXHORIZ) &&
            (blackbox_attrib.flags & BaseDisplay::ATTRIB_MAXVERT))
            maximized = true;
        else if (blackbox_attrib.flags & BaseDisplay::ATTRIB_MAXVERT)
            maximizeVertical();
        else if (blackbox_attrib.flags & BaseDisplay::ATTRIB_MAXHORIZ)
            maximizeHorizontal();

        blackbox_attrib.premax_x = x;
        blackbox_attrib.premax_y = y;
        blackbox_attrib.premax_w = w;
        blackbox_attrib.premax_h = h;
    }

    setState(current_state);
}

/**
   Show the window menu at pos mx, my
*/
void FluxboxWindow::showMenu(int mx, int my) {
    m_windowmenu.move(mx, my);
    m_windowmenu.show();		
    m_windowmenu.raise();
}

/**
   Moves the menu to last button press position and shows it,
   if it's already visible it'll be hidden
 */
void FluxboxWindow::popupMenu() {
    if (m_windowmenu.isVisible()) {
        m_windowmenu.hide(); 
        return;
    }
    // move menu directly under titlebar
    int diff_y = m_frame.titlebar().height() + m_frame.titlebar().borderWidth();
    if (!decorations.titlebar) // if we don't have any titlebar
        diff_y = 0;

    m_windowmenu.move(m_last_button_x, m_frame.y() + diff_y);
    m_windowmenu.show();
    m_windowmenu.raise();
}

void FluxboxWindow::restoreGravity() {
    // restore x coordinate
    switch (m_client->win_gravity) {
        // handle Westward gravity
    case NorthWestGravity:
    case WestGravity:
    case SouthWestGravity:
    default:
        m_client->x = m_frame.x();
        break;

    // handle Eastward gravity
    case NorthEastGravity:
    case EastGravity:
    case SouthEastGravity:
        m_client->x = (m_frame.x() + m_frame.width()) - m_client->width();
        break;
    }

    // restore y coordinate
    switch (m_client->win_gravity) {
        // handle Northbound gravity
    case NorthWestGravity:
    case NorthGravity:
    case NorthEastGravity:
    default:
        m_client->y = m_frame.y();
        break;

        // handle Southbound gravity
    case SouthWestGravity:
    case SouthGravity:
    case SouthEastGravity:
        m_client->y = (m_frame.y() + m_frame.height()) - m_client->height();
        break;
    }
}

/**
   Determine if this is the lowest tab of them all
*/
bool FluxboxWindow::isLowerTab() const {
    cerr<<__FILE__<<"("<<__FUNCTION__<<") TODO!"<<endl;
    return true;
}

/**
   Redirect any unhandled event to our handlers
*/
void FluxboxWindow::handleEvent(XEvent &event) {
    switch (event.type) {
    case ConfigureRequest:
        configureRequestEvent(event.xconfigurerequest);
        break;
    case MapNotify:
        mapNotifyEvent(event.xmap);
        break;
        // This is already handled in Fluxbox::handleEvent
        // case MapRequest:
        //        mapRequestEvent(event.xmaprequest);
        //break;
    case PropertyNotify:
        if (event.xproperty.state != PropertyDelete) {
            propertyNotifyEvent(event.xproperty.atom);
        }
        break;
    default:
        break;
    }
}

void FluxboxWindow::mapRequestEvent(XMapRequestEvent &re) {
    // we're only conserned about client window event
    if (re.window != m_client->window())
        return;

    Fluxbox *fluxbox = Fluxbox::instance();
	
    bool get_state_ret = getState();
    if (! (get_state_ret && fluxbox->isStartup())) {
        if ((m_client->wm_hint_flags & StateHint) &&
            (! (current_state == NormalState || current_state == IconicState))) {
            current_state = m_client->initial_state;
        } else
            current_state = NormalState;
    } else if (iconic)
        current_state = NormalState;
		
    switch (current_state) {
    case IconicState:
        iconify();
	break;

    case WithdrawnState:
        withdraw();
	break;

    case NormalState:
        // check WM_CLASS only when we changed state to NormalState from 
        // WithdrawnState (ICCC 4.1.2.5)
				
        XClassHint ch;

        if (XGetClassHint(display, getClientWindow(), &ch) == 0) {
            cerr<<"Failed to read class hint!"<<endl;
        } else {
            if (ch.res_name != 0) {
                m_instance_name = const_cast<char *>(ch.res_name);
                XFree(ch.res_name);
            } else
                m_instance_name = "";

            if (ch.res_class != 0) {
                m_class_name = const_cast<char *>(ch.res_class);
                XFree(ch.res_class);
            } else 
                m_class_name = "";

            /*
              Workspace *wsp = screen.getWorkspace(workspace_number);
              // we must be resizable AND maximizable to be autogrouped
              //!! TODO: there should be an isGroupable() function
              if (wsp != 0 && isResizable() && isMaximizable()) {
              wsp->checkGrouping(*this);
              }
            */
        }		
		
        deiconify(false);
			
	break;
    case InactiveState:
    case ZoomState:
    default:
        deiconify(false);
        break;
    }

}


void FluxboxWindow::mapNotifyEvent(XMapEvent &ne) {
    WinClient *client = findClient(ne.window);
    if (client == 0)
        return;

    if (!ne.override_redirect && isVisible()) {
        Fluxbox *fluxbox = Fluxbox::instance();
        fluxbox->grab();
        if (! validateClient())
            return;

        setState(NormalState);		
			
        if (client->isTransient() || screen.doFocusNew())
            setInputFocus();
        else
            setFocusFlag(false);			

	if (focused)
            m_frame.setFocus(true);

        iconic = false;

        // Auto-group from tab?
        if (!client->isTransient()) {
            cerr<<__FILE__<<"("<<__FUNCTION__<<") TODO check grouping here"<<endl;
        }

        fluxbox->ungrab();
    }
}

/**
   Unmaps frame window and client window if 
   event.window == m_client->window
   Returns true if *this should die
   else false
*/
void FluxboxWindow::unmapNotifyEvent(XUnmapEvent &ue) {
    WinClient *client = findClient(ue.window);
    if (client == 0)
        return;
	
#ifdef DEBUG
    cerr<<__FILE__<<"("<<__FUNCTION__<<"): 0x"<<hex<<client->window()<<dec<<endl;
    cerr<<__FILE__<<"("<<__FUNCTION__<<"): title="<<client->title()<<endl;
#endif // DEBUG
    
    restore(client, false);

}

/**
   Checks if event is for m_client->window.
*/
void FluxboxWindow::destroyNotifyEvent(XDestroyWindowEvent &de) {
    if (de.window == m_client->window()) {
#ifdef DEBUG
        cerr<<__FILE__<<"("<<__LINE__<<"): DestroyNotifyEvent this="<<this<<endl;
#endif // DEBUG
        if (numClients() == 1)
            m_frame.hide();
    }

}


void FluxboxWindow::propertyNotifyEvent(Atom atom) {

    switch(atom) {
    case XA_WM_CLASS:
    case XA_WM_CLIENT_MACHINE:
    case XA_WM_COMMAND:
        break;

    case XA_WM_TRANSIENT_FOR: {
        // TODO: this property notify should be handled by winclient
        // but for now we'll justhave to update all transient info
        //bool was_transient = isTransient();
        ClientList::iterator it = clientList().begin();
        ClientList::iterator it_end = clientList().end();
        for (; it != it_end; it++) 
            (*it)->updateTransientInfo();
        reconfigure();
        // TODO: this is broken whilst we don't know which client
        // update our layer to be the same layer as our transient for
        //if (isTransient() && isTransient() != was_transient)
        //    getLayerItem().setLayer(getTransientFor()->getLayerItem().getLayer());
            
    } break;

    case XA_WM_HINTS:
        getWMHints();
        break;

    case XA_WM_ICON_NAME:
        updateIconNameFromClient();
        updateIcon();
        break;

    case XA_WM_NAME:
        updateTitleFromClient();

        if (! iconic)
            screen.getWorkspace(workspace_number)->update();
        else
            updateIcon();
		 

        break;

    case XA_WM_NORMAL_HINTS: {
        getWMNormalHints();

        if ((m_client->normal_hint_flags & PMinSize) &&
            (m_client->normal_hint_flags & PMaxSize)) {

            if (m_client->max_width != 0 && m_client->max_width <= m_client->min_width &&
                m_client->max_height != 0 && m_client->max_height <= m_client->min_height) {
                decorations.maximize = false;
                decorations.handle = false;
                functions.resize=false;
                functions.maximize=false;
            } else {
                // TODO: is broken while handled by FbW, needs to be in WinClient
                if (! winClient().isTransient()) {
                    decorations.maximize = true;
                    decorations.handle = true;
                    functions.maximize = true;	        
                }
                functions.resize = true;
            }
 
    	}

        // save old values
        int x = m_frame.x(), y = m_frame.y();
        unsigned int w = m_frame.width(), h = m_frame.height();

        upsize();

        // reconfigure if the old values changed
        if (x != m_frame.x() || y != m_frame.y() ||
            w != m_frame.width() || h != m_frame.height()) {
            moveResize(x, y, w, h);
        }

        break; 
    }

    default:
        if (atom == FbAtoms::instance()->getWMProtocolsAtom()) {
            getWMProtocols();
            //!!TODO  check this area            
            // reset window actions
            screen.setupWindowActions(*this);
            
        } 
        break;
    }

}


void FluxboxWindow::exposeEvent(XExposeEvent &ee) {
    m_frame.exposeEvent(ee);
}


void FluxboxWindow::configureRequestEvent(XConfigureRequestEvent &cr) {
    WinClient *client = findClient(cr.window);
    if (client == 0)
        return;

    int cx = m_frame.x(), cy = m_frame.y();
    unsigned int cw = m_frame.width(), ch = m_frame.height();

    if (cr.value_mask & CWBorderWidth)
        client->old_bw = cr.border_width;

    if (cr.value_mask & CWX)
        cx = cr.x;

    if (cr.value_mask & CWY)
        cy = cr.y;

    if (cr.value_mask & CWWidth)
        cw = cr.width;

    if (cr.value_mask & CWHeight)
        ch = cr.height;

    // whether we should send ConfigureNotify to clients
    bool send_notify = false;

    // the request is for client window so we resize the frame to it first
    if (frame().width() != cw || frame().height() != ch) {        
        frame().resizeForClient(cw, ch);
        send_notify = true;
    }

    if (frame().x() != cx || frame().y() != cy) {
        move(cx, cy);
        // since we already send a notify in move we don't need to do that again
        send_notify = false;
    } 

    if (send_notify)
        sendConfigureNotify();

    if (cr.value_mask & CWStackMode) {
        switch (cr.detail) {
        case Above:
        case TopIf:
        default:
            raise();
            break;

        case Below:
        case BottomIf:
            lower();
            break;
        }
    }

}


void FluxboxWindow::buttonPressEvent(XButtonEvent &be) {
    m_last_button_x = be.x_root;
    m_last_button_y = be.y_root;

    // check frame events first
    m_frame.buttonPressEvent(be);

    if (be.button == 1 || (be.button == 3 && be.state == Mod1Mask)) {
        if ((! focused) && (! screen.isSloppyFocus())) { //check focus 
            setInputFocus(); 
        }

        if (m_frame.clientArea() == be.window) {            
            if (getScreen().clickRaises())
                raise();
            XAllowEvents(display, ReplayPointer, be.time);			
        } else {            
            button_grab_x = be.x_root - m_frame.x() - screen.rootTheme().borderWidth();
            button_grab_y = be.y_root - m_frame.y() - screen.rootTheme().borderWidth();      
        }
        
        if (m_windowmenu.isVisible())
            m_windowmenu.hide();
    }
}

void FluxboxWindow::shapeEvent(XShapeEvent *) { }

void FluxboxWindow::buttonReleaseEvent(XButtonEvent &re) {

    if (isMoving())
        stopMoving();		
    else if (isResizing())
        stopResizing();
    else if (m_attaching_tab)
        attachTo(re.x_root, re.y_root);
    else if (re.window == m_frame.window()) {
        if (re.button == 2 && re.state == Mod1Mask)
            XUngrabPointer(display, CurrentTime);
        else 
            m_frame.buttonReleaseEvent(re);
    } else {
        m_frame.buttonReleaseEvent(re);
    }
}


void FluxboxWindow::motionNotifyEvent(XMotionEvent &me) {
    if (isMoving() && me.window == screen.getRootWindow()) {
        me.window = m_frame.window().window();
    }
    bool inside_titlebar = (m_frame.titlebar() == me.window || m_frame.label() == me.window ||
                            m_frame.handle() == me.window || m_frame.window() == me.window);

    if (Fluxbox::instance()->getIgnoreBorder()
        && !(me.state & Mod1Mask) // really should check for exact matches
        && !(isMoving() || isResizing())) {
        int borderw = screen.rootTheme().borderWidth();
        if (me.x_root < (m_frame.x() + borderw) ||
            me.y_root < (m_frame.y() + borderw) ||
            me.x_root > (m_frame.x() + (int)m_frame.width() + borderw) ||
            me.y_root > (m_frame.y() + (int)m_frame.height() + borderw))
            return;
    }

    WinClient *client = 0;
    if (!inside_titlebar) {
        // determine if we're in titlebar
        Client2ButtonMap::iterator it = m_labelbuttons.begin();
        Client2ButtonMap::iterator it_end = m_labelbuttons.end();
        for (; it != it_end; ++it) {
            if ((*it).second->window() == me.window) {
                inside_titlebar = true;
                client = (*it).first;
                break;
            }
        }
    }

    if ((me.state & Button1Mask) && functions.move &&
        inside_titlebar && 
        !isResizing()) {

        if (! isMoving()) {
            startMoving(me.window);
        } else {
            int dx = me.x_root - button_grab_x, 
                dy = me.y_root - button_grab_y;

            dx -= screen.rootTheme().borderWidth();
            dy -= screen.rootTheme().borderWidth();

            // Warp to next or previous workspace?, must have moved sideways some
            int moved_x = me.x_root - last_resize_x;
            // save last event point
            last_resize_x = me.x_root;
            last_resize_y = me.y_root;

            if (moved_x && screen.isWorkspaceWarping()) {
                unsigned int cur_id = screen.getCurrentWorkspaceID();
                unsigned int new_id = cur_id;
                const int warpPad = screen.getEdgeSnapThreshold();
                // 1) if we're inside the border threshold
                // 2) if we moved in the right direction
                if (me.x_root >= int(screen.getWidth()) - warpPad - 1 &&
                    moved_x > 0) {
                    //warp right
                    new_id = (cur_id + 1) % screen.getCount();
                    dx = - me.x_root; // move mouse back to x=0
                } else if (me.x_root <= warpPad &&
                           moved_x < 0) {
                    //warp left
                    new_id = (cur_id + screen.getCount() - 1) % screen.getCount();
                    dx = screen.getWidth() - me.x_root-1; // move mouse to screen width - 1
                }
                if (new_id != cur_id) {
                    XWarpPointer(display, None, None, 0, 0, 0, 0, dx, 0);

                    screen.changeWorkspaceID(new_id);

                    last_resize_x = me.x_root + dx;
                    
                    // dx is the difference, so our new x is what it would  have been
                    // without the warp, plus the difference.
                    dx += me.x_root - button_grab_x;
                }
            }
            // dx = current left side, dy = current top
            doSnapping(dx, dy);
            
            if (! screen.doOpaqueMove()) {
                XDrawRectangle(display, screen.getRootWindow(), screen.rootTheme().opGC(),
                               last_move_x, last_move_y, 
                               m_frame.width() + 2*frame().window().borderWidth()-1,
                               m_frame.height() + 2*frame().window().borderWidth()-1);

                XDrawRectangle(display, screen.getRootWindow(), screen.rootTheme().opGC(),
                               dx, dy, 
                               m_frame.width() + 2*frame().window().borderWidth()-1,
                               m_frame.height() + 2*frame().window().borderWidth()-1);
                last_move_x = dx;
                last_move_y = dy;
            } else {
                moveResize(dx, dy, m_frame.width(), m_frame.height());
            }

            if (screen.doShowWindowPos())
                screen.showPosition(dx, dy);
        } // end if moving
    } else if (functions.resize &&
               (((me.state & Button1Mask) && (me.window == m_frame.gripRight() ||
                                              me.window == m_frame.gripLeft())) ||
                me.window == m_frame.window())) {

        bool left = (me.window == m_frame.gripLeft());

        if (! resizing) {			
            startResizing(me.window, me.x, me.y, left); 
        } else if (resizing) {
            // draw over old rect
            XDrawRectangle(display, screen.getRootWindow(), screen.rootTheme().opGC(),
                           last_resize_x, last_resize_y,
                           last_resize_w - 1 + 2 * m_frame.window().borderWidth(),
                           last_resize_h - 1 + 2 * m_frame.window().borderWidth());


            // move rectangle
            int gx = 0, gy = 0;

            last_resize_h = m_frame.height() + (me.y - button_grab_y);
            if (last_resize_h < 1)
                last_resize_h = 1;

            if (left) {
                last_resize_x = me.x_root - button_grab_x;
                if (last_resize_x > (signed) (m_frame.x() + m_frame.width()))
                    last_resize_x = last_resize_x + m_frame.width() - 1;

                left_fixsize(&gx, &gy);
            } else {
                last_resize_w = m_frame.width() + (me.x - button_grab_x);
                if (last_resize_w < 1) // clamp to 1
                    last_resize_w = 1;

                right_fixsize(&gx, &gy);
            }

            // draw resize rectangle
            XDrawRectangle(display, screen.getRootWindow(), screen.rootTheme().opGC(),
                           last_resize_x, last_resize_y,
                           last_resize_w - 1 + 2 * m_frame.window().borderWidth(), 
                           last_resize_h - 1 + 2 * m_frame.window().borderWidth());

            if (screen.doShowWindowPos())
                screen.showGeometry(gx, gy);
        }
    } else if ((me.state & Button2Mask) && inside_titlebar && client != 0) {

        //
        // drag'n'drop code for tabs
        //
        if (m_attaching_tab == 0) {
            // start drag'n'drop for tab
            m_attaching_tab = client;

            XGrabPointer(display, me.window, False, Button2MotionMask |
                         ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                         None, Fluxbox::instance()->getMoveCursor(), CurrentTime);
            last_move_x = me.x_root - 1;
            last_move_y = me.y_root - 1;
        
            XDrawRectangle(display, getScreen().getRootWindow(), 
                           getScreen().rootTheme().opGC(),
                           last_move_x, last_move_y,
                           m_labelbuttons[client]->width(), 
                           m_labelbuttons[client]->height());
        } else { 
            // we already grabed and started to drag'n'drop tab
            // so we update drag'n'drop-rectangle
            int dx = me.x_root - 1, dy = me.y_root - 1;

            dx -= getScreen().rootTheme().borderWidth();
            dy -= getScreen().rootTheme().borderWidth();

            if (getScreen().getEdgeSnapThreshold()) {
                int drx = getScreen().getWidth() - (dx + 1);

                if (dx > 0 && dx < drx && dx < getScreen().getEdgeSnapThreshold()) 
                    dx = 0;
                else if (drx > 0 && drx < getScreen().getEdgeSnapThreshold())
                    dx = getScreen().getWidth() - 1;

                int dty, dby;
		
                dty = dy;
                dby = -dy - 1;

                if (dy > 0 && dty < getScreen().getEdgeSnapThreshold())
                    dy = 0;
                else if (dby > 0 && dby < getScreen().getEdgeSnapThreshold())
                    dy = - 1;
		
            }
		
            //erase rectangle
            XDrawRectangle(display, getScreen().getRootWindow(),
                           getScreen().rootTheme().opGC(),
                           last_move_x, last_move_y, 
                           m_labelbuttons[client]->width(), 
                           m_labelbuttons[client]->height());


            //redraw rectangle at new pos
            last_move_x = dx;
            last_move_y = dy;			
            XDrawRectangle(display, getScreen().getRootWindow(), 
                           getScreen().rootTheme().opGC(),
                           last_move_x, last_move_y,
                           m_labelbuttons[client]->width(), 
                           m_labelbuttons[client]->height());

			
        }
    }

}

void FluxboxWindow::enterNotifyEvent(XCrossingEvent &ev) { 

    // ignore grab activates, or if we're not visible
    if (ev.mode == NotifyGrab ||
        !isVisible()) {
        return;
    }
    if (ev.window == frame().window() ||
        ev.window == m_client->window()) {
        if ((screen.isSloppyFocus() || screen.isSemiSloppyFocus()) 
            && !isFocused()) {
           
            // check that there aren't any subsequent leave notify events in the 
            // X event queue
            XEvent dummy;
            scanargs sa;
            sa.w = ev.window;
            sa.enter = sa.leave = False;
            XCheckIfEvent(display, &dummy, queueScanner, (char *) &sa);   

            if ((!sa.leave || sa.inferior) && setInputFocus())
                installColormap(True);
        }        
    }
}

void FluxboxWindow::leaveNotifyEvent(XCrossingEvent &ev) { 
    if (ev.window == frame().window())
        installColormap(false);
}

// TODO: functions should not be affected by decoration
void FluxboxWindow::setDecoration(Decoration decoration) {
    switch (decoration) {
    case DECOR_NONE:
        decorations.titlebar = decorations.border = decorations.handle =
            decorations.iconify = decorations.maximize =
            decorations.tab = false; //tab is also a decor
        decorations.menu = true; // menu is present
	//	functions.iconify = functions.maximize = true;
	//	functions.move = true;   // We need to move even without decor
	//	functions.resize = true; // We need to resize even without decor
	break;

    default:
    case DECOR_NORMAL:
        decorations.titlebar = decorations.border = decorations.handle =
            decorations.iconify = decorations.maximize =
            decorations.menu = true;
        functions.resize = functions.move = functions.iconify =
            functions.maximize = true;
	break;

    case DECOR_TINY:
        decorations.titlebar = decorations.iconify = decorations.menu =
            functions.move = functions.iconify = true;
        decorations.border = decorations.handle = decorations.maximize =
            functions.resize = functions.maximize = false;
	break;

    case DECOR_TOOL:
        decorations.titlebar = decorations.menu = functions.move = true;
        decorations.iconify = decorations.border = decorations.handle =
            decorations.maximize = functions.resize = functions.maximize =
            functions.iconify = false;
	break;
    }
    applyDecorations();
    // is this reconfigure necessary???
    reconfigure();

}

// commit current decoration values to actual displayed things
void FluxboxWindow::applyDecorations() {
    // we rely on frame not doing anything if it is already shown/hidden
    if (decorations.titlebar) 
        m_frame.showTitlebar();
    else
        m_frame.hideTitlebar();

    if (decorations.handle)
        m_frame.showHandle();
    else
        m_frame.hideHandle();

    m_frame.show();
    // is reconfigure needed here?
}

void FluxboxWindow::toggleDecoration() {
    //don't toggle decor if the window is shaded
    if (isShaded())
        return;
	
    if (decorations.enabled) { //remove decorations
        setDecoration(DECOR_NONE); 
        decorations.enabled = false;
    } else { //revert back to old decoration
        if (old_decoration == DECOR_NONE) { // make sure something happens
            setDecoration(DECOR_NORMAL);
        } else {
            setDecoration(old_decoration);
        }
        decorations.enabled = true;
    }
}

unsigned int FluxboxWindow::getDecorationMask() const {
    unsigned int ret = 0;
    if (decorations.titlebar)
        ret |= DECORM_TITLEBAR;
    if (decorations.handle)
        ret |= DECORM_HANDLE;
    if (decorations.border)
        ret |= DECORM_BORDER;
    if (decorations.iconify)
        ret |= DECORM_ICONIFY;
    if (decorations.maximize)
        ret |= DECORM_MAXIMIZE;
    if (decorations.close)
        ret |= DECORM_CLOSE;
    if (decorations.menu)
        ret |= DECORM_MENU;
    if (decorations.sticky)
        ret |= DECORM_STICKY;
    if (decorations.shade)
        ret |= DECORM_SHADE;
    if (decorations.tab)
        ret |= DECORM_TAB;
    if (decorations.enabled)
        ret |= DECORM_ENABLED;
    return ret;
}

void FluxboxWindow::setDecorationMask(unsigned int mask) {
    decorations.titlebar = mask & DECORM_TITLEBAR;
    decorations.handle   = mask & DECORM_HANDLE;
    decorations.border   = mask & DECORM_BORDER;
    decorations.iconify  = mask & DECORM_ICONIFY;
    decorations.maximize = mask & DECORM_MAXIMIZE;
    decorations.close    = mask & DECORM_CLOSE;
    decorations.menu     = mask & DECORM_MENU;
    decorations.sticky   = mask & DECORM_STICKY;
    decorations.shade    = mask & DECORM_SHADE;
    decorations.tab      = mask & DECORM_TAB;
    decorations.enabled  = mask & DECORM_ENABLED;
    applyDecorations();
}

bool FluxboxWindow::validateClient() {
    XSync(display, false);

    XEvent e;
    if (XCheckTypedWindowEvent(display, m_client->window(), DestroyNotify, &e) ||
        XCheckTypedWindowEvent(display, m_client->window(), UnmapNotify, &e)) {
        XPutBackEvent(display, &e);
        Fluxbox::instance()->ungrab();

        return false;
    }

    return true;
}

void FluxboxWindow::startMoving(Window win) {
    moving = true;
    Fluxbox *fluxbox = Fluxbox::instance();
    // grabbing (and masking) on the root window allows us to 
    // freely map and unmap the window we're moving.
    XGrabPointer(display, screen.getRootWindow(), False, Button1MotionMask |
                 ButtonReleaseMask, GrabModeAsync, GrabModeAsync,
                 screen.getRootWindow(), fluxbox->getMoveCursor(), CurrentTime);

    if (m_windowmenu.isVisible())
        m_windowmenu.hide();

    move_ws = workspace_number;
    fluxbox->maskWindowEvents(screen.getRootWindow(), this);

    last_move_x = frame().x();
    last_move_y = frame().y();
    if (! screen.doOpaqueMove()) {
        fluxbox->grab();
        XDrawRectangle(display, screen.getRootWindow(), screen.rootTheme().opGC(),
                       frame().x(), frame().y(),
                       frame().width() + 2*frame().window().borderWidth()-1, 
                       frame().height() + 2*frame().window().borderWidth()-1);
        screen.showPosition(frame().x(), frame().y());
    }
}

void FluxboxWindow::stopMoving() {
    moving = false;
    Fluxbox *fluxbox = Fluxbox::instance();

    fluxbox->maskWindowEvents(0, 0);

   
    if (! screen.doOpaqueMove()) {
        XDrawRectangle(FbTk::App::instance()->display(), screen.getRootWindow(), screen.rootTheme().opGC(),
                       last_move_x, last_move_y, 
                       frame().width() + 2*frame().window().borderWidth()-1,
                       frame().height() + 2*frame().window().borderWidth()-1);
        moveResize(last_move_x, last_move_y, m_frame.width(), m_frame.height());
        if (workspace_number != getScreen().getCurrentWorkspaceID()) {
            screen.reassociateWindow(this, getScreen().getCurrentWorkspaceID(), true);
            m_frame.show();
        }
        fluxbox->ungrab();
    } else
        moveResize(m_frame.x(), m_frame.y(), m_frame.width(), m_frame.height());

    screen.hideGeometry();
    XUngrabPointer(display, CurrentTime);
	
    XSync(display, False); //make sure the redraw is made before we continue
}

void FluxboxWindow::pauseMoving() {
    if (getScreen().doOpaqueMove()) {
        return;
    }

    XDrawRectangle(display, getScreen().getRootWindow(), 
                   getScreen().rootTheme().opGC(),
                   last_move_x, last_move_y, 
                   m_frame.width() + 2*frame().window().borderWidth()-1,
                   m_frame.height() + 2*frame().window().borderWidth()-1);
    
}


void FluxboxWindow::resumeMoving() {
    if (screen.doOpaqueMove()) {
        return;
    }
    
    if (workspace_number == screen.getCurrentWorkspaceID()) {
        m_frame.show();
    }
    XSync(display,false);
    XDrawRectangle(display, screen.getRootWindow(), screen.rootTheme().opGC(),
                   last_move_x, last_move_y, 
                   m_frame.width() + 2*frame().window().borderWidth()-1,
                   m_frame.height() + 2*frame().window().borderWidth()-1);

}

/**
 * Helper function that snaps a window to another window
 * We snap if we're closer than the x/ylimits.
 */
inline void snapToWindow(int &xlimit, int &ylimit, 
                         int left, int right, int top, int bottom,
                         int oleft, int oright, int otop, int obottom) {
    // Only snap if we're adjacent to the edge we're looking at

    // for left + right, need to be in the right y range
    if (top <= obottom && bottom >= otop) {
        // left
        if (abs(left-oleft)  < abs(xlimit)) xlimit = -(left-oleft);
        if (abs(right-oleft) < abs(xlimit)) xlimit = -(right-oleft);
        
        // right
        if (abs(left-oright)  < abs(xlimit)) xlimit = -(left-oright);
        if (abs(right-oright) < abs(xlimit)) xlimit = -(right-oright);
    }
    
    // for top + bottom, need to be in the right x range
    if (left <= oright && right >= oleft) {
        // top
        if (abs(top-otop)    < abs(ylimit)) ylimit = -(top-otop);
        if (abs(bottom-otop) < abs(ylimit)) ylimit = -(bottom-otop);
        
        // bottom
        if (abs(top-obottom)    < abs(ylimit)) ylimit = -(top-obottom);
        if (abs(bottom-obottom) < abs(ylimit)) ylimit = -(bottom-obottom);
    }
    
}

/*
 * Do Whatever snapping magic is necessary, and return using the left and top variables
 * to indicate the new x,y position
 */
void FluxboxWindow::doSnapping(int &orig_left, int &orig_top) {
    /*
     * Snap to screen edge
     * Snap to windows
     * Snap to toolbar
     * Snap to slit
     * TODO:
     * Xinerama screen edge?
     */

    if (screen.getEdgeSnapThreshold() == 0) return;

    // Keep track of our best offsets so far
    // We need to find things less than or equal to the threshold
    int dx = screen.getEdgeSnapThreshold() + 1;
    int dy = screen.getEdgeSnapThreshold() + 1;

    // we only care about the left/top etc that includes borders
    int borderW = m_frame.window().borderWidth();

    int top = orig_top; // orig include the borders
    int left = orig_left;
    int right = orig_left + getWidth() + 2*borderW;
    int bottom = orig_top + getHeight() + 2*borderW;

    /////////////////////////////////////
    // begin by checking the screen edges

    snapToWindow(dx, dy, left, right, top, bottom, 0, screen.getWidth(), 0, screen.getHeight());
    
    /////////////////////////////////////
    // now check window edges

    Workspace::Windows &wins = 
        screen.getCurrentWorkspace()->getWindowList();

    Workspace::Windows::iterator it = wins.begin();
    Workspace::Windows::iterator it_end = wins.end();

    for (; it != it_end; it++) {
        if ((*it) == this) continue; // skip myself

        snapToWindow(dx, dy, left, right, top, bottom, 
                     (*it)->getXFrame(),
                     (*it)->getXFrame() + (*it)->getWidth()  + 2*borderW,
                     (*it)->getYFrame(),
                     (*it)->getYFrame() + (*it)->getHeight() + 2*borderW);
    }

    /////////////////////////////////////
    // now the toolbar

    Toolbar *tbar = screen.getToolbar();
    if (tbar)
        snapToWindow(dx, dy, left, right, top, bottom, 
                     tbar->x(), tbar->x() + tbar->width() + 2*borderW,
                     tbar->y(), tbar->y() + tbar->height() + 2*borderW);

    /////////////////////////////////////
    // and the slit

#ifdef SLIT
    Slit *slit = screen.getSlit();
    if (slit) 
        snapToWindow(dx, dy, left, right, top, bottom, 
                     slit->x(), slit->x() + slit->width() + 2*borderW,
                     slit->y(), slit->y() + slit->height() + 2*borderW);
#endif // SLIT

    // commit
    if (dx <= screen.getEdgeSnapThreshold()) 
        orig_left += dx;
    if (dy <= screen.getEdgeSnapThreshold()) 
        orig_top  += dy;

}


void FluxboxWindow::startResizing(Window win, int x, int y, bool left) {
    resizing = true;
    Fluxbox *fluxbox = Fluxbox::instance();
    XGrabPointer(display, win, false, ButtonMotionMask | ButtonReleaseMask, 
                 GrabModeAsync, GrabModeAsync, None,
                 (left ? fluxbox->getLowerLeftAngleCursor() : fluxbox->getLowerRightAngleCursor()),
                 CurrentTime);

    int gx = 0, gy = 0;
    button_grab_x = x;
    button_grab_y = y;
    last_resize_x = m_frame.x();
    last_resize_y = m_frame.y();
    last_resize_w = m_frame.width();
    last_resize_h = m_frame.height();

    if (left)
        left_fixsize(&gx, &gy);
    else
        right_fixsize(&gx, &gy);

    if (screen.doShowWindowPos())
        screen.showGeometry(gx, gy);

    XDrawRectangle(display, screen.getRootWindow(), screen.rootTheme().opGC(),
                   last_resize_x, last_resize_y,
                   last_resize_w - 1 + 2 * m_frame.window().borderWidth(),
                   last_resize_h - 1 + 2 * m_frame.window().borderWidth());
}

void FluxboxWindow::stopResizing(Window win) {
    resizing = false;
	
    XDrawRectangle(display, screen.getRootWindow(), screen.rootTheme().opGC(),
                   last_resize_x, last_resize_y,
                   last_resize_w - 1 + 2 * m_frame.window().borderWidth(),
                   last_resize_h - 1 + 2 * m_frame.window().borderWidth());

    screen.hideGeometry();

    if (win && win == m_frame.gripLeft())
        left_fixsize();
    else
        right_fixsize();

	
    moveResize(last_resize_x, last_resize_y,
               last_resize_w,
               last_resize_h);
	
    XUngrabPointer(display, CurrentTime);
}

void FluxboxWindow::attachTo(int x, int y) {
    if (m_attaching_tab == 0)
        return;

    XUngrabPointer(display, CurrentTime);


    XDrawRectangle(display, getScreen().getRootWindow(),
                   getScreen().rootTheme().opGC(),
                   last_move_x, last_move_y, 
                   m_labelbuttons[m_attaching_tab]->width(), 
                   m_labelbuttons[m_attaching_tab]->height());
            
    int dest_x = 0, dest_y = 0;
    Window child = 0;

    if (XTranslateCoordinates(display, getScreen().getRootWindow(), 
                              getScreen().getRootWindow(),
                              x, y, &dest_x, &dest_y, &child)) {        
        // search for a fluxboxwindow 
        FluxboxWindow *attach_to_win = Fluxbox::instance()->searchWindow(child);

        if (attach_to_win != this &&
            attach_to_win != 0) {
            attach_to_win->attachClient(*m_attaching_tab);
        } else if (attach_to_win != this) { // disconnect client if we didn't drop on a window
            detachClient(*m_attaching_tab);
        }
                    
    }
    m_attaching_tab = 0;
}

//finds and redraw the icon label
void FluxboxWindow::updateIcon() {
    if (screen.getToolbar()) {
        const IconBar *iconbar = 0;
        const IconBarObj *icon = 0;
        if ((iconbar = screen.getToolbar()->iconBar()) != 0) {
            if ((icon = iconbar->findIcon(this)) != 0)
                iconbar->draw(icon, icon->width());
        }
    }
}

void FluxboxWindow::restore(WinClient *client, bool remap) {
    if (client->m_win != this)
        return;

    XChangeSaveSet(display, client->window(), SetModeDelete);
    client->setEventMask(NoEventMask);

    //!! TODO
    //restoreGravity();

    client->hide();

    // restore old border width
    client->setBorderWidth(client->old_bw);

    XEvent not_used;
    if (! XCheckTypedWindowEvent(display, client->window(), ReparentNotify,
                                 &not_used)) {
#ifdef DEBUG
        cerr<<"FluxboxWindow::restore: reparent 0x"<<hex<<client->window()<<dec<<" to root"<<endl;
#endif // DEBUG

        // reparent to root window
        client->reparent(screen.getRootWindow(), m_frame.x(), m_frame.y());        
    }

    if (remap)
        client->show();

    delete client;

#ifdef DEBUG
        cerr<<__FILE__<<"("<<__FUNCTION__<<"): numClients() = "<<numClients()<<endl;
#endif // DEBUG
    if (numClients() == 0) {

        m_frame.hide();
    }

}

void FluxboxWindow::restore(bool remap) {
    if (numClients() == 0)
        return;

    while (!clientList().empty()) {
        restore(clientList().back(), remap);
    }
}

void FluxboxWindow::timeout() {
    raise();
}

Window FluxboxWindow::getClientWindow() const  { 
    if (m_client == 0)
        return 0;
    return m_client->window(); 
}

const std::string &FluxboxWindow::getTitle() const { 
    static string empty_string("");
    if (m_client == 0)
        return empty_string;
    return m_client->title();
}

const std::string &FluxboxWindow::getIconTitle() const { 
    static string empty_string("");
    if (m_client == 0)
        return empty_string;
    return m_client->iconTitle();
}

int FluxboxWindow::getXClient() const { return m_client->x; }
int FluxboxWindow::getYClient() const { return m_client->y; }
unsigned int FluxboxWindow::getClientHeight() const { return m_client->height(); }
unsigned int FluxboxWindow::getClientWidth() const { return m_client->width(); }
int FluxboxWindow::initialState() const { return m_client->initial_state; }

void FluxboxWindow::changeBlackboxHints(const BaseDisplay::BlackboxHints &net) {
    if ((net.flags & BaseDisplay::ATTRIB_SHADED) &&
        ((blackbox_attrib.attrib & BaseDisplay::ATTRIB_SHADED) !=
         (net.attrib & BaseDisplay::ATTRIB_SHADED)))
        shade();

    if ((net.flags & (BaseDisplay::ATTRIB_MAXVERT | BaseDisplay::ATTRIB_MAXHORIZ)) &&
        ((blackbox_attrib.attrib & (BaseDisplay::ATTRIB_MAXVERT | BaseDisplay::ATTRIB_MAXHORIZ)) !=
         (net.attrib & (BaseDisplay::ATTRIB_MAXVERT | BaseDisplay::ATTRIB_MAXHORIZ)))) {
        if (maximized) {
            maximize();
        } else {
            if ((net.flags & BaseDisplay::ATTRIB_MAXHORIZ) && (net.flags & BaseDisplay::ATTRIB_MAXVERT))
            	maximize();
            else if (net.flags & BaseDisplay::ATTRIB_MAXVERT)
                maximizeVertical();
            else if (net.flags & BaseDisplay::ATTRIB_MAXHORIZ)
                maximizeHorizontal();

        }
    }
    
    if ((net.flags & BaseDisplay::ATTRIB_OMNIPRESENT) &&
        ((blackbox_attrib.attrib & BaseDisplay::ATTRIB_OMNIPRESENT) !=
         (net.attrib & BaseDisplay::ATTRIB_OMNIPRESENT)))
        stick();

    if ((net.flags & BaseDisplay::ATTRIB_WORKSPACE) &&
        (workspace_number !=  net.workspace)) {

        screen.reassociateWindow(this, net.workspace, true);

        if (screen.getCurrentWorkspaceID() != net.workspace)
            withdraw();
        else 
            deiconify();
    }

    if (net.flags & BaseDisplay::ATTRIB_STACK) {
        if ((unsigned int) m_layernum != net.stack) {
            moveToLayer(net.stack);
        }
    }

    if (net.flags & BaseDisplay::ATTRIB_DECORATION) {
        old_decoration = static_cast<Decoration>(net.decoration);
        setDecoration(old_decoration);
    }

}

void FluxboxWindow::upsize() {
    m_frame.setBevel(screen.rootTheme().bevelWidth());
    m_frame.handle().resize(m_frame.handle().width(), 
                            screen.rootTheme().handleWidth());
    m_frame.gripLeft().resize(m_frame.buttonHeight(), 
                              screen.rootTheme().handleWidth());
    m_frame.gripRight().resize(m_frame.gripLeft().width(), 
                               m_frame.gripLeft().height());
}


///TODO
void FluxboxWindow::downsize() {
	
}


void FluxboxWindow::right_fixsize(int *gx, int *gy) {
    // calculate the size of the client window and conform it to the
    // size specified by the size hints of the client window...
    int dx = last_resize_w - m_client->base_width;
    int titlebar_height = (decorations.titlebar ? 
                           frame().titlebar().height() +
                           frame().titlebar().borderWidth() : 0);
    int handle_height = (decorations.handle ? 
                         frame().handle().height() +
                         frame().handle().borderWidth() : 0);

    int dy = last_resize_h - m_client->base_height - titlebar_height - handle_height;
    if (dx < (signed) m_client->min_width)
        dx = m_client->min_width;
    if (dy < (signed) m_client->min_height)
        dy = m_client->min_height;
    if (m_client->max_width > 0 && (unsigned) dx > m_client->max_width)
        dx = m_client->max_width;
    if (m_client->max_height > 0 && (unsigned) dy > m_client->max_height)
        dy = m_client->max_height;

    // make it snap

    if (m_client->width_inc == 0)
        m_client->width_inc = 1;
    if (m_client->height_inc == 0)
        m_client->height_inc = 1;

    dx /= m_client->width_inc;
    dy /= m_client->height_inc;

    if (gx) *gx = dx;
    if (gy) *gy = dy;

    dx = (dx * m_client->width_inc) + m_client->base_width;
    dy = (dy * m_client->height_inc) + m_client->base_height + 
        titlebar_height + handle_height;
    last_resize_w = dx;
    last_resize_h = dy;
}

void FluxboxWindow::left_fixsize(int *gx, int *gy) {   
    int titlebar_height = (decorations.titlebar ? 
                           frame().titlebar().height()  + 
                           frame().titlebar().borderWidth() : 0);
    int handle_height = (decorations.handle ? 
                         frame().handle().height() + 
                         frame().handle().borderWidth() : 0);
    int decoration_height = titlebar_height + handle_height;

    // dx is new width = current width + difference between new and old x values
    int dx = m_frame.width() + m_frame.x() - last_resize_x;

    // dy = new height (w/o decorations), similarly
    int dy = last_resize_h - m_client->base_height - decoration_height;

    // check minimum size
    if (dx < static_cast<signed int>(m_client->min_width))
        dx = m_client->min_width;
    if (dy < static_cast<signed int>(m_client->min_height))
        dy = m_client->min_height;

    // check maximum size
    if (m_client->max_width > 0 && dx > static_cast<signed int>(m_client->max_width))
        dx = m_client->max_width;
    if (m_client->max_height > 0 && dy > static_cast<signed int>(m_client->max_height))
        dy = m_client->max_height;

    // make sure we have valid increment
    if (m_client->width_inc == 0)
        m_client->width_inc = 1;
    if (m_client->height_inc == 0)
        m_client->height_inc = 1;

    // set snapping
    dx /= m_client->width_inc;
    dy /= m_client->height_inc;

    // set return values
    if (gx != 0)
        *gx = dx;
    if (gy != 0)
        *gy = dy;

    // snapping
    dx = dx * m_client->width_inc + m_client->base_width;
    dy = dy * m_client->height_inc + m_client->base_height + decoration_height;

    // update last resize 
    last_resize_w = dx;
    last_resize_h = dy;
    last_resize_x = m_frame.x() + m_frame.width() - last_resize_w;	
}

void FluxboxWindow::resizeClient(WinClient &client, 
                                 unsigned int height, unsigned int width) {
    client.resize(m_frame.clientArea().width(),
                  m_frame.clientArea().height());
    client.updateRect(m_frame.x() + m_frame.clientArea().x(),
                      m_frame.y() + m_frame.clientArea().y(),
                      m_frame.clientArea().width(),
                      m_frame.clientArea().height());    
}

void FluxboxWindow::sendConfigureNotify() {
    ClientList::iterator client_it = m_clientlist.begin();
    ClientList::iterator client_it_end = m_clientlist.end();
    for (; client_it != client_it_end; ++client_it) {
        WinClient &client = *(*client_it);
        /*
          Send event telling where the root position 
          of the client window is. (ie frame pos + client pos inside the frame = send pos)
        */
        //!!
        client.x = m_frame.x();
        client.y = m_frame.y();
        resizeClient(client, 
                     m_frame.clientArea().width(),
                     m_frame.clientArea().height());

        
        XEvent event;
        event.type = ConfigureNotify;

        event.xconfigure.display = display;
        event.xconfigure.event = client.window();
        event.xconfigure.window = client.window();
        event.xconfigure.x = m_frame.x() + m_frame.clientArea().x();
        event.xconfigure.y = m_frame.y() + m_frame.clientArea().y();
        event.xconfigure.width = client.width();
        event.xconfigure.height = client.height();
        event.xconfigure.border_width = client.old_bw;
        event.xconfigure.above = m_frame.window().window();
        event.xconfigure.override_redirect = false;

        screen.updateNetizenConfigNotify(&event);
    } // end for        
}

