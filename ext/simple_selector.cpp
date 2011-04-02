/**
 * simple_selector.cpp
 * swift
 *
 * @version 0.1
 */

#include <stdio.h>
#include <utility>
#include <deque>
#include <list>
#include "swift.h"

using namespace swift;

class SimpleSelector : public PeerSelector {

public:

    SimpleSelector () {
    }

    /**
     * Add a new peer for the given root hash.
     *
     * @param {Address} addr	Address of the new peer.
     * @param {Sha1Hash} root	Root hash.
     */
    void AddPeer (const Address& addr, const Sha1Hash& root) {

    	FileTransfer* file = FileTransfer::Find(root);

    	if(file){
    		// Add the peer on this file transfer.
    		file->OnPexIn(addr);
    	}
    }

    /**
     * Add a list of peers for the given root hash.
     *
     * @param {PeerList} peerList	List of peers.
     * @param {Sha1Hash} root		Rooth hash.
     */
    void AddPeers (PeerList* peerList, const Sha1Hash& root) {

    	FileTransfer* file = FileTransfer::Find(root);

    	if(file){
    		// Add the peer on this file transfer.
    		PeerList::iterator it;

    		for (it = peerList->begin(); it != peerList->end(); it++){
    			file->OnPexIn(*it);
    		}
    	}
    }

    /**
     * Return a current peer of the given root hash.
     *
     * @param {Sha1Hash} for_root	Root hash.
     * @return {Address} peer		Address of the peer.
     */
    Address* GetPeer (const Sha1Hash& for_root) {

    	Address* peer = NULL;
    	Channel* ch = NULL;
    	int i = 0;

    	// Find the first peer available for this root hash.
    	while(!peer && i<Channel::Nchannels()){

    		ch = Channel::channel(i++);

            if (ch && ch->transfer().root_hash()==for_root){
            	/**
            	 * FIXME Check other ways to return the desired object.
            	 * The method peer() is a pointer to a private variable at channel object.
            	 * Copy the data to prevent possible heap memory errors.
            	 */
            	peer = new Address(ch->peer().ipv4(), ch->peer().port());
            }
        }

    	// Don't forget to free this data when it will not be used!
        return peer;
    }

    /**
     * Return the addresses of the current peers given a root hash.
     *
     * @param {Sha1Hash}	for_root	Root hash.
     * @return {PeerList}	peerList	List of peers. @see list.
     */
    PeerList* GetPeers (const Sha1Hash& for_root){

    	PeerList* peerList = new PeerList;
    	Channel* ch = NULL;

    	// Find all the peers for this root hash.
    	for(int i=0; i<Channel::Nchannels(); i++) {

			ch = Channel::channel(i);

            if (ch && ch->transfer().root_hash()==for_root){
            	peerList->push_back(ch->peer());
            }
            i++;
        }

    	// Don't forget to free this list when it will not be used!
        return peerList;
    }
};
