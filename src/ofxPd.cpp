/*
 * Copyright (c) 2011 Dan Wilcox <danomatika@gmail.com>
 *
 * BSD Simplified License.
 * For information on usage and redistribution, and for a DISCLAIMER OF ALL
 * WARRANTIES, see the file, "LICENSE.txt," in this distribution.
 *
 * See https://github.com/danomatika/ofxPd for documentation
 *
 * This project uses libpd, copyrighted by Miller Puckette and others using the
 * "Standard Improved BSD License". See the file "LICENSE.txt" in src/pd.
 *
 * See http://gitorious.org/pdlib/pages/Libpd for documentation
 *
 */
#include "ofxPd.h"

#include <algorithm>

#include "ofUtils.h"
#include <Poco/Mutex.h>

// needed for libpd audio passing
#ifndef USEAPI_DUMMY
	#define USEAPI_DUMMY
#endif

using namespace std;
using namespace pd;

// used to lock libpd for thread safety
Poco::Mutex mutex;

#define _LOCK() mutex.lock()
#define _UNLOCK() mutex.unlock()

//--------------------------------------------------------------------
ofxPd::ofxPd() : PdBase() {
    ticksPerBuffer = 32;
	inputBuffer = NULL;
    clear();
}

//--------------------------------------------------------------------
ofxPd::~ofxPd() {
    clear();
}

//--------------------------------------------------------------------
bool ofxPd::init(const int numOutChannels, const int numInChannels, 
				 const int sampleRate, const int ticksPerBuffer) {
	
	this->ticksPerBuffer = ticksPerBuffer;			 
	
	// init pd
	_LOCK();
	if(!PdBase::init(numInChannels, numOutChannels, sampleRate)) {
		_UNLOCK();
		ofLog(OF_LOG_ERROR, "Pd: Could not init");
        clear();
		return false;
	}
	_UNLOCK();
    
    // allocate buffers
	inputBuffer = new float[numInChannels*ticksPerBuffer*blockSize()];

	ofLogVerbose("Pd") <<"inited";
	ofLogVerbose("Pd") <<" samplerate: " << sampleRate;
	ofLogVerbose("Pd") <<" channels in: " << numInChannels;
	ofLogVerbose("Pd") <<" out: " << numOutChannels;
	ofLogVerbose("Pd") <<" ticks: " << ticksPerBuffer;
	ofLogVerbose("Pd") <<" block size: " << blockSize();
    ofLogVerbose("Pd") <<" calc buffer size: " << ticksPerBuffer*blockSize();
    
    return true;
}

void ofxPd::clear() {
	_LOCK();
	if(inputBuffer != NULL) {
        delete[] inputBuffer;
        inputBuffer = NULL;
    }
    PdBase::clear();
	_UNLOCK();
    
    channels.clear();
    
    // add default global channel
	Channel c;
	channels.insert(make_pair(-1, c));
}

//--------------------------------------------------------------------
void ofxPd::addToSearchPath(const std::string& path) {
	string fullpath = ofFilePath::getAbsolutePath(ofToDataPath(path));
	ofLogVerbose("Pd") << "adding search path: "+fullpath;
	_LOCK();
	PdBase::addToSearchPath(fullpath.c_str());
	_UNLOCK();
}
		
void ofxPd::clearSearchPath() {
	ofLogVerbose("Pd") << "clearing search paths";
	_LOCK();
	PdBase::clearSearchPath();
	_UNLOCK();
}

//--------------------------------------------------------------------
Patch ofxPd::openPatch(const std::string& patch) {

	string fullpath = ofFilePath::getAbsolutePath(ofToDataPath(patch));
	string file = ofFilePath::getFileName(fullpath);
	string folder = ofFilePath::getEnclosingDirectory(fullpath);
	
	// trim the trailing slash Poco::Path always adds ... blarg
	if(folder.size() > 0 && folder.at(folder.size()-1) == '/') {
		folder.erase(folder.end()-1);
	}
	
	ofLogVerbose("Pd") << "opening patch: "+file+" path: "+folder;

	// [; pd open file folder(
	_LOCK();
    Patch p = PdBase::openPatch(file.c_str(), folder.c_str());
    _UNLOCK();
    if(!p.isValid()) {
		ofLogError("Pd") << "opening patch \""+file+"\" failed";
	}
	
	return p;
}

pd::Patch ofxPd::openPatch(pd::Patch& patch) {
	
	ofLogVerbose("Pd") << "opening patch: "+patch.filename()+" path: "+patch.path();
	
	_LOCK();
	Patch p = PdBase::openPatch(patch);
	_UNLOCK();
	if(!p.isValid()) {
		ofLogError("Pd") << "opening patch \""+patch.filename()+"\" failed";
	}
	
	return p;
}

void ofxPd::closePatch(const std::string& patch) {

	ofLogVerbose("Pd") << "closing path: "+patch;

	_LOCK();
	PdBase::closePatch(patch);
	_UNLOCK();
}

void ofxPd::closePatch(Patch& patch) {
	
	ofLogVerbose("Pd") << "closing patch: "+patch.filename();
	
	_LOCK();
	PdBase::closePatch(patch);
	_UNLOCK();
}	

//--------------------------------------------------------------------
void ofxPd::computeAudio(bool state) {
    if(state)
        ofLogVerbose("Pd") << "audio processing on";
    else
        ofLogVerbose("Pd") << "audio processing off";
    
    // [; pd dsp $1(
	_LOCK();
	PdBase::computeAudio(state);
	_UNLOCK();
}
void ofxPd::start() {
    // [; pd dsp 1(
	computeAudio(true);
}

void ofxPd::stop() {	
	// [; pd dsp 0(
	computeAudio(false);
}

//----------------------------------------------------------
void ofxPd::subscribe(const std::string& source) {

	if(exists(source)) {
		ofLogWarning("Pd") << "subscribe: ignoring duplicate source";
		return;
	}
	
    PdBase::subscribe(source);
	Source s;
	sources.insert(pair<string,Source>(source, s));
}

void ofxPd::unsubscribe(const std::string& source) {
	
	map<string,Source>::iterator iter;
	iter = sources.find(source);
	if(iter == sources.end()) {
		ofLogWarning("Pd") << "unsubscribe: ignoring unknown source";
		return;
	}
	
    PdBase::unsubscribe(source);
	sources.erase(iter);
}

bool ofxPd::exists(const std::string& source) {
	if(sources.find(source) != sources.end())
		return true;
	return false;
}

void ofxPd::unsubscribeAll(){
	
    PdBase::unsubscribeAll();
	sources.clear();

	// add default global source
	Source s;
	sources.insert(make_pair("", s));
}

//--------------------------------------------------------------------
void ofxPd::addReceiver(PdReceiver& receiver) {
	
	pair<set<PdReceiver*>::iterator, bool> ret;
	ret = receivers.insert(&receiver);
	if(!ret.second) {
		ofLogWarning("Pd") << "addReceiver: ignoring duplicate receiver";
		return;
	}
	
	// set PdBase receiver on adding first reciever
	if(receivers.size() == 1) {
		PdBase::setReceiver(this);
	}
    
    // receive from all sources by default
    receive(receiver);
}

void ofxPd::removeReceiver(PdReceiver& receiver) {
	
	// exists?
	set<PdReceiver*>::iterator r_iter;
	r_iter = receivers.find(&receiver);
	if(r_iter == receivers.end()) {
		ofLogWarning("Pd") << "removeReceiver: ignoring unknown receiver";
		return;
	}
	receivers.erase(r_iter);
	
	// clear PdBase receiver on removing last reciever
	if(receivers.size() == 0) {
		PdBase::setReceiver(NULL);
	}

	// remove from all sources
	ignore(receiver);		
}

bool ofxPd::receiverExists(PdReceiver& receiver) {
	if(receivers.find(&receiver) != receivers.end())
		return true;
	return false;
}

void ofxPd::clearReceivers() {

	receivers.clear();
	
	map<string,Source>::iterator iter;
	for(iter = sources.begin(); iter != sources.end(); ++iter) {
		iter->second.receivers.clear();
	}
	
	PdBase::setReceiver(NULL);
}

//----------------------------------------------------------
void ofxPd::receive(PdReceiver& receiver, const std::string& source) {

	if(!receiverExists(receiver)) {
		ofLogWarning("Pd") << "receive: unknown receiver, call addReceiver first";
		return;
	}
	
	if(!exists(source)) {
		ofLogWarning("Pd") << "receive: unknown source, call subscribe first";
		return;
	}
	
	// global source (all sources)
	map<string,Source>::iterator g_iter;
	g_iter = sources.find("");
	
	// subscribe to specific source
	if(source != "") {
	
		// make sure global source is ignored
		if(g_iter->second.receiverExists(&receiver)) {
			g_iter->second.removeReceiver(&receiver);
		}
		
		// receive from specific source
		map<string,Source>::iterator s_iter;
		s_iter = sources.find(source);
		s_iter->second.addReceiver(&receiver);
	}
	else {
		// make sure all sources are ignored
		ignore(receiver);
	
		// receive from the global source
		g_iter->second.addReceiver(&receiver);
	}
}

void ofxPd::ignore(PdReceiver& receiver, const std::string& source) {

	if(!receiverExists(receiver)) {
		ofLogWarning("Pd") << "ignore: ignoring unknown receiver";
		return;
	}

	if(!exists(source)) {
		ofLogWarning("Pd") << "ignore: ignoring unknown source";
		return;
	}
	
	map<string,Source>::iterator s_iter;
	
	// unsubscribe from specific source
	if(source != "") {
	
		// global source (all sources)
		map<string,Source>::iterator g_iter;
		g_iter = sources.find("");
	
		// negation from global
		if(g_iter->second.receiverExists(&receiver)) {
			
			// remove from global
			g_iter->second.removeReceiver(&receiver);
			
			// add to *all* other sources
			for(s_iter = sources.begin(); s_iter != sources.end(); ++s_iter) {
				if(s_iter != g_iter) {
					s_iter->second.addReceiver(&receiver);
				}
			}
		}
		
		// remove from source
		s_iter = sources.find(source);
		s_iter->second.removeReceiver(&receiver);
	}
	else {	// ignore all sources	
		for(s_iter = sources.begin(); s_iter != sources.end(); ++s_iter) {
			s_iter->second.removeReceiver(&receiver);
		}
	}
}

bool ofxPd::isReceiving(PdReceiver& receiver, const std::string& source) {
	map<string,Source>::iterator s_iter;
	s_iter = sources.find(source);
	if(s_iter != sources.end() && s_iter->second.receiverExists(&receiver))
		return true;
	return false;
}

//----------------------------------------------------------
void ofxPd::addMidiReceiver(PdMidiReceiver& receiver) {
	
    pair<set<PdMidiReceiver*>::iterator, bool> ret;
	ret = midiReceivers.insert(&receiver);
	if(!ret.second) {
		ofLogWarning("Pd") << "addMidiReceiver: ignoring duplicate receiver";
		return;
	}
    
	// set PdBase receiver on adding first reciever
	if(midiReceivers.size() == 1) {
		PdBase::setMidiReceiver(this);
	}
	
    // receive from all channels by default
    receiveMidi(receiver);
}

void ofxPd::removeMidiReceiver(PdMidiReceiver& receiver) {

	// exists?
	set<PdMidiReceiver*>::iterator r_iter;
	r_iter = midiReceivers.find(&receiver);
	if(r_iter == midiReceivers.end()) {
		ofLogWarning("Pd") << "removeMidiReceiver: ignoring unknown receiver";
		return;
	}
	midiReceivers.erase(r_iter);
	
	// clear PdBase receiver on removing last reciever
	if(receivers.size() == 0) {
		PdBase::setMidiReceiver(NULL);
	}

	// remove from all sources
	ignoreMidi(receiver);	
}

bool ofxPd::midiReceiverExists(PdMidiReceiver& receiver) {
	if(midiReceivers.find(&receiver) != midiReceivers.end())
		return true;
	return false;
}

void ofxPd::clearMidiReceivers() {

	midiReceivers.clear();
	
	map<int,Channel>::iterator iter;
	for(iter = channels.begin(); iter != channels.end(); ++iter) {
		iter->second.receivers.clear();
	}
	
	PdBase::setMidiReceiver(NULL);
}

//----------------------------------------------------------
void ofxPd::receiveMidi(PdMidiReceiver& receiver, int channel) {

	if(!midiReceiverExists(receiver)) {
		ofLogWarning("Pd") << "receiveMidi: unknown receiver, call addMidiReceiver first";
		return;
	}
	
    // handle bad channel numbers
    if(channel < 0)
        channel = 0;
    
    // insert channel if it dosen't exist yet
    if(channels.find(channel) == channels.end()) {
        Channel c;
        channels.insert(pair<int,Channel>(channel, c));
    }
    
	// global channel (all channels)
	map<int,Channel>::iterator g_iter;
	g_iter = channels.find(0);
	
	// subscribe to specific channel
	if(channel != 0) {
	
		// make sure global channel is ignored
		if(g_iter->second.midiReceiverExists(&receiver)) {
			g_iter->second.removeMidiReceiver(&receiver);
		}
		
		// receive from specific channel
		map<int,Channel>::iterator c_iter;
		c_iter = channels.find(channel);
		c_iter->second.addMidiReceiver(&receiver);
	}
	else {
		// make sure all channels are ignored
		ignoreMidi(receiver);
	
		// receive from the global channel
		g_iter->second.addMidiReceiver(&receiver);
	}
}

void ofxPd::ignoreMidi(PdMidiReceiver& receiver, int channel) {

	if(!midiReceiverExists(receiver)) {
		ofLogWarning("Pd") << "ignoreMidi: ignoring unknown receiver";
		return;
	} 
	
    // handle bad channel numbers
    if(channel < 0)
        channel = 0;
    
    // insert channel if it dosen't exist yet
    if(channels.find(channel) == channels.end()) {
        Channel c;
        channels.insert(pair<int,Channel>(channel, c));
    }
    
	map<int,Channel>::iterator c_iter;
	
	// unsubscribe from specific channel
	if(channel != 0) {
	
		// global channel (all channels)
		map<int,Channel>::iterator g_iter;
		g_iter = channels.find(0);
	
		// negation from global
		if(g_iter->second.midiReceiverExists(&receiver)) {
			
			// remove from global
			g_iter->second.removeMidiReceiver(&receiver);
			
			// add to *all* other channels
			for(c_iter = channels.begin(); c_iter != channels.end(); ++c_iter) {
				if(c_iter != g_iter) {
					c_iter->second.addMidiReceiver(&receiver);
				}
			}
		}
		
		// remove from channel
		c_iter = channels.find(channel);
		c_iter->second.removeMidiReceiver(&receiver);
	}
	else {	// ignore all sources	
		for(c_iter = channels.begin(); c_iter != channels.end(); ++c_iter) {
			c_iter->second.removeMidiReceiver(&receiver);
		}
	}
}

bool ofxPd::isReceivingMidi(PdMidiReceiver& receiver, int channel) {
	
    // handle bad channel numbers
    if(channel < 0)
        channel = 0;
    
    map<int,Channel>::iterator c_iter;
	c_iter = channels.find(channel);
	if(c_iter != channels.end() && c_iter->second.midiReceiverExists(&receiver))
		return true;
	return false;
}

//----------------------------------------------------------
void ofxPd::sendBang(const std::string& dest) {
	_LOCK();
	PdBase::sendBang(dest);
	_UNLOCK();
}

void ofxPd::sendFloat(const std::string& dest, float value) {
	_LOCK();
	PdBase::sendFloat(dest, value);
	_UNLOCK();
}

void ofxPd::sendSymbol(const std::string& dest, const std::string& symbol) {
	_LOCK();
	PdBase::sendSymbol(dest, symbol);
	_UNLOCK();
}

//----------------------------------------------------------
void ofxPd::startMessage() {
	_LOCK();	
	PdBase::startMessage();
	_UNLOCK();
}

void ofxPd::addFloat(const float value) {
	_LOCK();
	PdBase::addFloat(value);
	_UNLOCK();
}

void ofxPd::addSymbol(const std::string& symbol) {
	_LOCK();
	PdBase::addSymbol(symbol);
	_UNLOCK();
}

void ofxPd::finishList(const std::string& dest) {
    _LOCK();
    PdBase::finishList(dest);
    _UNLOCK();
}

void ofxPd::finishMessage(const std::string& dest, const std::string& msg) {
    _LOCK();
    PdBase::finishMessage(dest, msg);
    _UNLOCK();
}

//----------------------------------------------------------
void ofxPd::sendList(const std::string& dest, const List& list) {
	_LOCK();	
	PdBase::sendList(dest, list);
	_UNLOCK();
}

void ofxPd::sendMessage(const std::string& dest, const std::string& msg, const List& list) {
	_LOCK();	
	PdBase::sendMessage(dest, msg, list);
	_UNLOCK();
}

//----------------------------------------------------------
void ofxPd::sendNoteOn(const int channel, const int pitch, const int velocity) {
	_LOCK();
	PdBase::sendNoteOn(channel-1, pitch, velocity);
	_UNLOCK();
}

void ofxPd::sendControlChange(const int channel, const int control, const int value) {
	_LOCK();
	PdBase::sendControlChange(channel-1, control, value);
	_UNLOCK();
}

void ofxPd::sendProgramChange(const int channel, int program) {
	_LOCK();
	PdBase::sendProgramChange(channel-1, program-1);
	_UNLOCK();
}

void ofxPd::sendPitchBend(const int channel, const int value) {
	_LOCK();
	PdBase::sendPitchBend(channel-1, value);
	_UNLOCK();
}

void ofxPd::sendAftertouch(const int channel, const int value) {
	_LOCK();
	PdBase::sendAftertouch(channel-1, value);
	_UNLOCK();
}

void ofxPd::sendPolyAftertouch(const int channel, int pitch, int value) {
	_LOCK();
	PdBase::sendPolyAftertouch(channel-1, pitch, value);
	_UNLOCK();
}

//----------------------------------------------------------
void ofxPd::sendMidiByte(const int port, const int value) {
	_LOCK();
	PdBase::sendMidiByte(port, value);
	_UNLOCK();
}

void ofxPd::sendSysex(const int port, const int value) {
	_LOCK();
	PdBase::sendSysex(port, value);
	_UNLOCK();
}

void ofxPd::sendSysRealTime(const int port, const int value) {
	_LOCK();
	PdBase::sendSysRealTime(port, value);
	_UNLOCK();
}

//----------------------------------------------------------
int ofxPd::arraySize(const std::string& arrayName) {
	_LOCK();
	int len = PdBase::arraySize(arrayName);
	_UNLOCK();
	return len;
}
		
bool ofxPd::readArray(const std::string& arrayName, std::vector<float>& dest, int readLen, int offset) {
	_LOCK();
	bool ret = PdBase::readArray(arrayName, dest, readLen, offset);
	_UNLOCK();
    return ret;
}
		
bool ofxPd::writeArray(const std::string& arrayName, std::vector<float>& source, int writeLen, int offset) {
    _LOCK();
	bool ret = PdBase::writeArray(arrayName, source, writeLen, offset);
	_UNLOCK();
    return ret;
}

void ofxPd::clearArray(const std::string& arrayName, int value) {
    _LOCK();
	PdBase::clearArray(arrayName, value);
	_UNLOCK();
}

//----------------------------------------------------------
void ofxPd::audioIn(float* input, int bufferSize, int nChannels) {
	try {
		if(inputBuffer != NULL) {
            _LOCK();
			memcpy(inputBuffer, input, bufferSize*nChannels*sizeof(float));
            _UNLOCK();
        }
	}
	catch (...) {
		ofLogError("Pd") << "could not copy input buffer, " <<
			"check your buffer size and num channels";
	}
}

void ofxPd::audioOut(float* output, int bufferSize, int nChannels) {
    if(inputBuffer != NULL) {
        _LOCK();
        if(!PdBase::processFloat(ticksPerBuffer, inputBuffer, output)) {
            ofLogError("Pd") << "could not process output buffer, " <<
                "check your buffer size and num channels";
        }
        _UNLOCK();
    }
}

/* ***** PROTECTED ***** */

//----------------------------------------------------------
void ofxPd::print(const std::string& message) {

    ofLogVerbose("Pd") << "print: " << message;
    
    // broadcast
    set<PdReceiver*>::iterator iter;
    for(iter = receivers.begin(); iter != receivers.end(); ++iter) {
        (*iter)->print(message);
    }
}

void ofxPd::receiveBang(const std::string& dest) {
    
    ofLogVerbose("Pd") << "bang: " << dest;
	
	set<PdReceiver*>::iterator r_iter;
	set<PdReceiver*>* r_set;
    
	// send to global receivers
	map<string,Source>::iterator g_iter;
	g_iter = sources.find("");
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveBang(dest);
	}
	
	// send to subscribed receivers
	map<string,Source>::iterator s_iter;
	s_iter = sources.find(dest);
	r_set = &s_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveBang(dest);
	}
}

void ofxPd::receiveFloat(const std::string& dest, float value) {
	
	ofLogVerbose("Pd") << "float: " << dest << " " << value;
	
	set<PdReceiver*>::iterator r_iter;
    set<PdReceiver*>* r_set;
	
	// send to global receivers
	map<string,Source>::iterator g_iter;
	g_iter = sources.find("");
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveFloat(dest, value);
	}
	
	// send to subscribed receivers
	map<string,Source>::iterator s_iter;
	s_iter = sources.find(dest);
	r_set = &s_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveFloat(dest, value);
	}
}

void ofxPd::receiveSymbol(const std::string& dest, const std::string& symbol) {
	
    ofLogVerbose("Pd") << "symbol: " << dest << " " << symbol;
	
	set<PdReceiver*>::iterator r_iter;
	set<PdReceiver*>* r_set;
	
	// send to global receivers
	map<string,Source>::iterator g_iter;
	g_iter = sources.find("");
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveSymbol(dest, (string) symbol);
	}
	
	// send to subscribed receivers
	map<string,Source>::iterator s_iter;
	s_iter = sources.find(dest);
	r_set = &s_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveSymbol(dest, (string) symbol);
	}
}

void ofxPd::receiveList(const std::string& dest, const List& list) {
	
    ofLogVerbose("Pd") << "list: " << dest << " " << list.toString();
	
	set<PdReceiver*>::iterator r_iter;
	set<PdReceiver*>* r_set;
	
	// send to global receivers
	map<string,Source>::iterator g_iter;
	g_iter = sources.find("");
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveList(dest, list);
	}
	
	// send to subscribed receivers
	map<string,Source>::iterator s_iter;
	s_iter = sources.find(dest);
	r_set = &s_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveList(dest, list);
	}
}

void ofxPd::receiveMessage(const std::string& dest, const std::string& msg, const List& list) {

    ofLogVerbose("Pd") << "message: " << dest << " " << msg << " " << list.toString();

	set<PdReceiver*>::iterator r_iter;
	set<PdReceiver*>* r_set;
	
	// send to global receivers
	map<string,Source>::iterator g_iter;
	g_iter = sources.find("");
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveMessage(dest, msg, list);
	}
	
	// send to subscribed receivers
	map<string,Source>::iterator s_iter;
	s_iter = sources.find(dest);
	r_set = &s_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveMessage(dest, msg, list);
	}
}

//----------------------------------------------------------
void ofxPd::receiveNoteOn(const int channel, const int pitch, const int velocity) {

	ofLogVerbose("Pd") << "note on: " << channel+1 << " " << pitch << " " << velocity;
	
    set<PdMidiReceiver*>::iterator r_iter;
	set<PdMidiReceiver*>* r_set;
    
	// send to global receivers
	map<int,Channel>::iterator g_iter;
	g_iter = channels.find(0);
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveNoteOn(channel+1, pitch, velocity);
	}
	
	// send to subscribed receivers
	map<int,Channel>::iterator c_iter;
	c_iter = channels.find(channel);
    if(c_iter != channels.end()) {
        r_set = &c_iter->second.receivers;
        for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
            (*r_iter)->receiveNoteOn(channel+1, pitch, velocity);
        }
    }
}

void ofxPd::receiveControlChange(const int channel, const int controller, const int value) {

	ofLogVerbose("Pd") << "control change: " << channel+1 << " " << controller << " " << value;

    set<PdMidiReceiver*>::iterator r_iter;
	set<PdMidiReceiver*>* r_set;
    
	// send to global receivers
	map<int,Channel>::iterator g_iter;
	g_iter = channels.find(0);
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveControlChange(channel+1, controller, value);
	}
	
	// send to subscribed receivers
	map<int,Channel>::iterator c_iter;
	c_iter = channels.find(channel);
    if(c_iter != channels.end()) {
        r_set = &c_iter->second.receivers;
        for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
            (*r_iter)->receiveControlChange(channel+1, controller, value);
        }
    }
}

void ofxPd::receiveProgramChange(const int channel, const int value) {

	ofLogVerbose("Pd") << "program change: " << channel+1 << " " << value+1;

    set<PdMidiReceiver*>::iterator r_iter;
	set<PdMidiReceiver*>* r_set;
    
	// send to global receivers
	map<int,Channel>::iterator g_iter;
	g_iter = channels.find(0);
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveProgramChange(channel+1, value+1);
	}
	
	// send to subscribed receivers
	map<int,Channel>::iterator c_iter;
	c_iter = channels.find(channel);
    if(c_iter != channels.end()) {
        r_set = &c_iter->second.receivers;
        for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
            (*r_iter)->receiveProgramChange(channel+1, value+1);
        }
    }
}

void ofxPd::receivePitchBend(const int channel, const int value) {

	ofLogVerbose("Pd") << "pitch bend: " << channel+1 << value;

    set<PdMidiReceiver*>::iterator r_iter;
	set<PdMidiReceiver*>* r_set;
    
	// send to global receivers
	map<int,Channel>::iterator g_iter;
	g_iter = channels.find(0);
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receivePitchBend(channel+1, value);
	}
	
	// send to subscribed receivers
	map<int,Channel>::iterator c_iter;
	c_iter = channels.find(channel);
    if(c_iter != channels.end()) {
        r_set = &c_iter->second.receivers;
        for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
            (*r_iter)->receivePitchBend(channel+1, value);
        }
    }
}

void ofxPd::receiveAftertouch(const int channel, const int value) {

	ofLogVerbose("Pd") << "aftertouch: " << channel+1 << value;

    set<PdMidiReceiver*>::iterator r_iter;
	set<PdMidiReceiver*>* r_set;
    
	// send to global receivers
	map<int,Channel>::iterator g_iter;
	g_iter = channels.find(0);
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receiveAftertouch(channel+1, value);
	}
	
	// send to subscribed receivers
	map<int,Channel>::iterator c_iter;
	c_iter = channels.find(channel);
    if(c_iter != channels.end()) {
        r_set = &c_iter->second.receivers;
        for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
            (*r_iter)->receiveAftertouch(channel+1, value);
        }
    }
}

void ofxPd::receivePolyAftertouch(const int channel, const int pitch, const int value) {

	ofLogVerbose("Pd") << "poly aftertouch: " << channel+1 << " " << pitch << " " << value;

    set<PdMidiReceiver*>::iterator r_iter;
	set<PdMidiReceiver*>* r_set;
    
	// send to global receivers
	map<int,Channel>::iterator g_iter;
	g_iter = channels.find(0);
	r_set = &g_iter->second.receivers;
	for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
		(*r_iter)->receivePolyAftertouch(channel+1, pitch, value);
	}
	
	// send to subscribed receivers
	map<int,Channel>::iterator c_iter;
	c_iter = channels.find(channel);
    if(c_iter != channels.end()) {
        r_set = &c_iter->second.receivers;
        for(r_iter = r_set->begin(); r_iter != r_set->end(); ++r_iter) {
            (*r_iter)->receivePolyAftertouch(channel+1, pitch, value);
        }
    }
}

void ofxPd::receiveMidiByte(const int port, const int byte) {

	ofLogVerbose("Pd") << "midi byte: " << port << " " << byte;

	set<PdMidiReceiver*>& r_set = midiReceivers;
	set<PdMidiReceiver*>::iterator iter;
	for(iter = r_set.begin(); iter != r_set.end(); ++iter) {
		(*iter)->receiveMidiByte(port, byte);
	}
}
