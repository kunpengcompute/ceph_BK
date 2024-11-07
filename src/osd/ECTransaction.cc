// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Inktank Storage, Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#include <iostream>
#include <vector>
#include <sstream>

#include "ECTransaction.h"
#include "ECUtil.h"
#include "os/ObjectStore.h"
#include "common/inline_variant.h"

void prepare_update_chunks(const ECUtil::stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ecimpl,
  DoutPrefixProvider *dpp,
  unsigned int offset,
  bufferlist &orig_bl, bufferlist &update_bl,
  map<int, bufferlist> &orig_chunks,  map<int,bufferlist> &update_chunks) {
  unsigned int  k = ecimpl->get_data_chunk_count();
  uint64_t stripe_width = sinfo.get_stripe_width();
  uint64_t chunk_size = sinfo.get_chunk_size();
  unsigned int index = (offset % stripe_width) / chunk_size;
  unsigned int count = orig_bl.length() / chunk_size >= k ? k : orig_bl.length() / chunk_size;
  ldpp_dout(dpp,20) << __func__ << ":length " << orig_bl.length() << " index: " << index << "count: " << count
	            << dendl;
  unsigned chunk_len = count == k ? orig_bl.length() / k : chunk_size;
  for ( unsigned int i = index; i < count + index; i++) {
    int chunk_index = ecimpl->chunk_index(i);
    orig_chunks[chunk_index].substr_of(orig_bl, (i - index) * chunk_len, chunk_len);
    update_chunks[chunk_index].substr_of(update_bl, (i-index) * chunk_len, chunk_len);
  }
}

void prepare_update_chunks_internal_stripe(const ECUtil::stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ecimpl, DoutPrefixProvider *dpp,
  unsigned int offset, bufferlist &orig_bl, bufferlist &update_bl,
  map<int, bufferlist> &orig_chunks, 
  map<int, bufferlist> &update_chunks_for_encode,
  map<int, pair<uint64_t, bufferlist>> &update_chunks) {
  uint64_t chunk_size = sinfo.get_chunk_size();
  uint64_t stripe_width = sinfo.get_stripe_width();
  unsigned int index = (offset % stripe_width) / chunk_size;
  unsigned int count = ((offset + orig_bl.length() - 1) % stripe_width) / chunk_size - index + 1;
  int chunk_index = 0;
  ldpp_dout(dpp,20) << __func__ << ":length " << orig_bl.length() << " index: " << index 
	            << "count: " << count << dendl;
  if ( count == 1) {
    chunk_index = ecimpl->chunk_index(index);
    orig_chunks[chunk_index].substr_of(orig_bl, 0, orig_bl.length());
    update_chunks[chunk_index].second.substr_of(update_bl, 0, update_bl.length());
    update_chunks[chunk_index].first = sinfo.logical_offset_to_chunk_unit_offset(offset);
    update_chunks_for_encode[chunk_index].substr_of(update_bl, 0, update_bl.length());
  } else {
    uint64_t len = orig_bl.length();
    uint64_t left = len;
    uint64_t chunk_len = 0;
    if (offset % chunk_size != 0) {
       chunk_len = chunk_size - (offset%chunk_size);
       ldpp_dout(dpp, 20) << __func__ << " first : length " << chunk_len << dendl;
       chunk_index = ecimpl->chunk_index(index);
       bufferlist orig_bl_tmp;
       bufferlist update_bl_tmp;
       orig_bl_tmp.append_zero(chunk_size);
       update_bl_tmp.append_zero(chunk_size);
       orig_bl_tmp.copy_in(chunk_size - chunk_len, chunk_len, orig_bl);
       update_bl_tmp.copy_in(chunk_size-chunk_len, chunk_len, update_bl);
       orig_chunks[chunk_index].append(orig_bl_tmp);
       update_chunks_for_encode[chunk_index].append(update_bl_tmp);
       update_chunks[chunk_index].second.substr_of(update_bl, 0, chunk_len);
       update_chunks[chunk_index].first = sinfo.logical_offset_to_chunk_unit_offset(offset);
       offset += chunk_len;
       left -= chunk_len;
       index++;
    }
    chunk_len = (chunk_size - (offset % chunk_size)) < left ? (chunk_size - (offset % chunk_size)) : left;
    while ( chunk_len == chunk_size) {
       chunk_index = ecimpl->chunk_index(index);
       orig_chunks[chunk_index].substr_of(orig_bl, len - left, chunk_len);
       update_chunks[chunk_index].second.substr_of(update_bl, len - left, chunk_len);
       update_chunks[chunk_index].first = sinfo.logical_offset_to_chunk_unit_offset(offset);
       update_chunks_for_encode[chunk_index].substr_of(update_bl, len - left, chunk_len);
       offset += chunk_len;
       left -= chunk_len;
       chunk_len = (chunk_size- (offset % chunk_size)) < left ? (chunk_size - (offset % chunk_size)) : left;
       index++;
    }
    if (left != 0) {
       ldpp_dout(dpp, 20) << __func__ << " last : length " << left << dendl;
       chunk_index = ecimpl->chunk_index(index);
       bufferlist orig_bl_tmp;
       bufferlist update_bl_tmp;
       orig_bl_tmp.append_zero(chunk_size);
       update_bl_tmp.append_zero(chunk_size);
       bufferlist orig_bl_left;
       bufferlist update_bl_left;
       orig_bl_left.substr_of(orig_bl, len - left, left);
       update_bl_left.substr_of(update_bl, len - left, left);
       orig_bl_tmp.copy_in(0, left, orig_bl_left);
       update_bl_tmp.copy_in(0, left, update_bl_left);
       orig_chunks[chunk_index].append(orig_bl_tmp);
       update_chunks_for_encode[chunk_index].append(update_bl_tmp);
       update_chunks[chunk_index].second.substr_of(update_bl, len - left, left);
       update_chunks[chunk_index].first = sinfo.logical_offset_to_chunk_unit_offset(offset);
    }
  }
}

void get_chunks_to_update ( const ECUtil::stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ecimpl,
  DoutPrefixProvider *dpp,
  uint64_t offset, bufferlist &orig_bl, bufferlist &update_bl,
  map<int, extent_map> &update_chunks) {
  uint64_t length = orig_bl.length();
  uint64_t end = offset + length;
  uint64_t stripe_width = sinfo.get_stripe_width();
  uint64_t chunk_size = sinfo.get_chunk_size();

  ldpp_dout(dpp, 20) << __func__ << ": " << " offset: " << offset << " len: " << orig_bl.length() << " update len " << update_bl.length() << dendl;
  if ((offset / stripe_width == (end - 1) / stripe_width) && length !=  stripe_width) {
    map<int, bufferlist> orig_chunks;
    map<int, pair<uint64_t, bufferlist>> tmp_update_chunks;
    map<int, bufferlist> tmp_update_chunks_for_encode;
    prepare_update_chunks_internal_stripe(sinfo, ecimpl, dpp, offset, orig_bl, update_bl, orig_chunks,
		                          tmp_update_chunks_for_encode, tmp_update_chunks);
    int r = ECUtil::encode_update(sinfo, ecimpl, orig_chunks, tmp_update_chunks_for_encode);
    ceph_assert(r == 0);
    for (auto i = tmp_update_chunks.begin(); i != tmp_update_chunks.end(); i++) {
      ldpp_dout(dpp, 20) << __func__ << " id " << i->first << " chunk off " << i->second.first
	                 << " len " << i->second.second.length() << dendl;
      update_chunks[i->first].insert(i->second.first, i->second.second.length(), i->second.second);
    }
    uint64_t chunk_off = length - (chunk_size - (offset % chunk_size)) == 0 ? sinfo.logical_offset_to_chunk_unit_offset(offset) : 
	               sinfo.logical_to_prev_chunk_offset(offset);
    for (unsigned int i = ecimpl->get_data_chunk_count(); i < ecimpl->get_chunk_count();i++) {
      ldpp_dout(dpp, 20) << __func__ << " id " << i << "chunk off " << chunk_off
	                 << " len " << tmp_update_chunks_for_encode[i].length() << dendl;
      update_chunks[i].insert(chunk_off, tmp_update_chunks_for_encode[i].length(),
		              tmp_update_chunks_for_encode[i]);
    }
  } else {
    uint64_t head_len;
    uint64_t tail_off;
    uint64_t tail_len;
    if (isp2(stripe_width)) {
      head_len = p2nphase(offset, stripe_width);
      tail_off = p2align(end,stripe_width);
      tail_len = p2phase(end,stripe_width);
    } else {
      head_len = sinfo.logical_to_next_stripe_offset(offset) - offset;
      tail_off = sinfo.logical_to_prev_stripe_offset(end);
      tail_len = end - sinfo.logical_to_prev_stripe_offset(end);
    }
    uint64_t middle_off = offset + head_len;
    uint64_t middle_len = length - head_len - tail_len;

    if (head_len) {
      ldpp_dout(dpp, 20) << __func__ << ": head_len " << head_len << dendl;  
      map<int, bufferlist> orig_chunks;
      map<int, pair<uint64_t, bufferlist>> tmp_update_chunks;
      map<int, bufferlist> tmp_update_chunks_for_encode;
      bufferlist _orig_bl;
      bufferlist _update_bl;
      _orig_bl.substr_of(orig_bl, 0, head_len);
      _update_bl.substr_of(update_bl, 0, head_len);
      prepare_update_chunks_internal_stripe(sinfo, ecimpl, dpp, offset, _orig_bl,_update_bl, orig_chunks,
 		                            tmp_update_chunks_for_encode, tmp_update_chunks);
      int r = ECUtil::encode_update(sinfo, ecimpl, orig_chunks, tmp_update_chunks_for_encode);
      ceph_assert(r == 0);
      for (auto i = tmp_update_chunks.begin(); i != tmp_update_chunks.end(); i++) {
        ldpp_dout(dpp, 20) << __func__ << " id " << i->first << "head chunk off " << i->second.first
	                   << " len " << i->second.second.length() << dendl;
        update_chunks[i->first].insert(i->second.first, i->second.second.length(), i->second.second);
      }
      uint64_t chunk_off = head_len - (chunk_size - (offset % chunk_size)) == 0 ? sinfo.logical_offset_to_chunk_unit_offset(offset) : 
	                   sinfo.logical_to_prev_chunk_offset(offset);
      for (unsigned int i = ecimpl->get_data_chunk_count(); i < ecimpl->get_chunk_count();i++) {
        ldpp_dout(dpp, 20) << __func__ << " id " << i << "head chunk off " << chunk_off 
 		           << " len " << tmp_update_chunks_for_encode[i].length() << dendl;
	update_chunks[i].insert(chunk_off,tmp_update_chunks_for_encode[i].length(),
			        tmp_update_chunks_for_encode[i]);
      }
    }

    if (middle_len) {
      ldpp_dout(dpp, 20) << __func__ << ": middle_off " << middle_off << "middle_len" << middle_len << dendl;  
      map<int, bufferlist> orig_chunks;
      map<int, bufferlist> tmp_update_chunks;
      bufferlist _orig_bl;
      bufferlist _update_bl;
      _orig_bl.substr_of(orig_bl, middle_off - offset, middle_len );
      _update_bl.substr_of(update_bl, middle_off - offset, middle_len);
      prepare_update_chunks(sinfo, ecimpl, dpp, middle_off, _orig_bl, _update_bl, orig_chunks, tmp_update_chunks);
      int r = ECUtil::encode_update(sinfo, ecimpl, orig_chunks, tmp_update_chunks);
      ceph_assert(r == 0);
      for (auto i = tmp_update_chunks.begin(); i != tmp_update_chunks.end(); i++) {
        ldpp_dout(dpp, 20) << __func__ << " id " << i->first << "middle chunk off " << sinfo.logical_to_prev_chunk_offset(middle_off)
	                   << " len " << i->second.length() << dendl;
        update_chunks[i->first].insert(sinfo.logical_to_prev_chunk_offset(middle_off), i->second.length(), i->second);
      }
    }

    if (tail_len) {
      ldpp_dout(dpp, 20) << __func__ << ": tail_off " << tail_off << "tail_len" << tail_len << dendl;  
      map<int, bufferlist> orig_chunks;
      map<int, bufferlist> tmp_update_chunks_for_encode;
      map<int, pair<uint64_t, bufferlist>> tmp_update_chunks;
      bufferlist _orig_bl;
      bufferlist _update_bl;
      _orig_bl.substr_of(orig_bl, tail_off - offset, tail_len );
      _update_bl.substr_of(update_bl, tail_off - offset, tail_len);
      prepare_update_chunks_internal_stripe(sinfo, ecimpl, dpp, tail_off, _orig_bl, _update_bl, orig_chunks,
		                            tmp_update_chunks_for_encode, tmp_update_chunks);
      int r = ECUtil::encode_update(sinfo, ecimpl, orig_chunks, tmp_update_chunks_for_encode);
      ceph_assert(r == 0);
      for (auto i = tmp_update_chunks.begin(); i != tmp_update_chunks.end(); i++) {
        ldpp_dout(dpp, 20) << __func__ << " id " << i->first << "tail chunk off " << i->second.first
	                   << " len " << i->second.second.length() << dendl;
        update_chunks[i->first].insert(i->second.first, i->second.second.length(), i->second.second);
      }
      for (unsigned int i = ecimpl->get_data_chunk_count(); i < ecimpl->get_chunk_count(); i++) {
        ldpp_dout(dpp, 20) << __func__ << " id " << i << "tail chunk off " << sinfo.logical_to_prev_chunk_offset(tail_off)
	                   << " len " << tmp_update_chunks_for_encode[i].length() << dendl;
        update_chunks[i].insert(sinfo.logical_to_prev_chunk_offset(tail_off), tmp_update_chunks_for_encode[i].length(),
			        tmp_update_chunks_for_encode[i]);
      }
    }
  }
}


void encode_and_write_update(
  pg_t pgid,
  const hobject_t &oid,
  const ECUtil::stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ecimpl,
  uint64_t offset,
  bufferlist orig_bl,
  bufferlist update_bl,
  uint32_t flags,
  ECUtil::HashInfoRef hinfo,
  extent_map &written,
  map<shard_id_t, ObjectStore::Transaction> *transactions,
  DoutPrefixProvider *dpp) {
  ceph_assert(orig_bl.length() == update_bl.length());
  ceph_assert(orig_bl.length() % sinfo.get_chunk_unit_size() == 0);
  ldpp_dout(dpp, 20) << __func__ << ": " << oid << " offset: " << offset << "len: " << orig_bl.length()
	             << dendl;

  map<int, bufferlist> buffers;
  map<int, bufferlist> orig_chunks;
  written.insert(offset, update_bl.length(), update_bl);
  map<int, extent_map> update_chunks;
 
  get_chunks_to_update(sinfo, ecimpl, dpp, offset, orig_bl, update_bl, update_chunks);
  set<int> code_chunk;
  for (unsigned int i = ecimpl->get_data_chunk_count(); i < ecimpl->get_chunk_count(); i++) {
    code_chunk.insert(ecimpl->chunk_index(i));
  }

  for (auto i = transactions->begin(); i !=transactions->end();) {
    if (!update_chunks.count((*i).first)) {
	i++;
	continue;
    }
    ceph_assert(update_chunks[(*i).first].ext_count() == 1);
    const bufferlist &enc_bl = update_chunks[(*i).first].begin().get_val();
    ldpp_dout(dpp, 20) << __func__ << " shard id:" <<(*i).first << dendl;

    if (code_chunk.count((*i).first)) {
      flags |= CEPH_OSD_OP_FLAG_FADVISE_UPDATE;
    }
    (*i).second.write(
      coll_t(spg_t(pgid, (*i).first)),
      ghobject_t(oid, ghobject_t::NO_GEN, (*i).first),
      update_chunks[(*i).first].begin().get_off(),
      enc_bl.length(),
      enc_bl,
      flags);
      i++;
  }
  ldpp_dout(dpp,20) << __func__ << "trans size :" << transactions->size() << dendl;
}
  
void encode_and_write(
  pg_t pgid,
  const hobject_t &oid,
  const ECUtil::stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ecimpl,
  const set<int> &want,
  uint64_t offset,
  bufferlist bl,
  uint32_t flags,
  ECUtil::HashInfoRef hinfo,
  extent_map &written,
  map<shard_id_t, ObjectStore::Transaction> *transactions,
  map<int, std::pair<uint64_t, uint64_t>> &zero_map,
  DoutPrefixProvider *dpp) {
  const uint64_t before_size = hinfo->get_total_logical_size(sinfo);
  ceph_assert(sinfo.logical_offset_is_stripe_aligned(offset));
  ceph_assert(sinfo.logical_offset_is_stripe_aligned(bl.length()));
  ceph_assert(bl.length());

  map<int, bufferlist> buffers;
  int r = ECUtil::encode(
    sinfo, ecimpl, bl, want, &buffers);
  ceph_assert(r == 0);

  written.insert(offset, bl.length(), bl);

  ldpp_dout(dpp, 20) << __func__ << ": " << oid
		     << " new_size "
		     << offset + bl.length()
		     << dendl;

  if (offset >= before_size) {
    ceph_assert(offset == before_size);
    hinfo->append(
      sinfo.aligned_logical_offset_to_chunk_offset(offset),
      buffers);
  }

  for (auto &&i : *transactions) {
    ceph_assert(buffers.count(i.first));
	bufferlist &enc_bl = buffers[i.first];
	if (offset >= before_size) {
	  i.second.set_alloc_hint(
	coll_t(spg_t(pgid,i.first)),
	ghobject_t(oid, ghobject_t::NO_GEN, i.first),
	0, 0,
	CEPH_OSD_ALLOC_HINT_FLAG_SEQUENTIAL_WRITE |
	CEPH_OSD_ALLOC_HINT_FLAG_APPEND_ONLY);
	}
      if (zero_map.find(i.first) != zero_map.end()) {
	uint64_t write_offset = sinfo.logical_to_prev_chunk_offset(offset);
	uint64_t zero_offset = zero_map[i.first].first;
	ldpp_dout(dpp,20) << __func__ << " zero_offset = " << zero_offset 
			  << ", write_offset = " << write_offset << dendl;
	if (write_offset < zero_offset) {
	  bufferlist write_buf;
	  uint64_t write_length = zero_offset - write_offset;
	  write_buf.substr_of(enc_bl, 0, write_length);
	  i.second.write(
	    coll_t(spg_t(pgid,i.first)),
	    ghobject_t(oid,ghobject_t::NO_GEN,i.first),
	    write_offset,
	    write_length,
 	    write_buf,
	    flags);
	  }
	  i.second.zero(
	  coll_t(spg_t(pgid,i.first)),
	  ghobject_t(oid,ghobject_t::NO_GEN,i.first),
	  zero_offset,
	  zero_map[i.first].second);
	} else {
	  i.second.write(
	  coll_t(spg_t(pgid,i.first)),
  	  ghobject_t(oid,ghobject_t::NO_GEN,i.first),
	  sinfo.logical_to_prev_chunk_offset(offset),
	  enc_bl.length(),
	  enc_bl,
	  flags);
	}
      }
    }

void encode_and_write_append(
	pg_t pgid,
	const hobject_t &oid,
	const ECUtil::stripe_info_t &sinfo,
	ErasureCodeInterfaceRef &ecimpl,
	const set<int> &want,
	uint64_t offset,
	bufferlist bl,
	uint32_t flags,
	ECUtil::HashInfoRef hinfo,
	extent_map &written,
	map<shard_id_t, ObjectStore::Transaction> *transactions,
	set<shard_id_t> &write_sid,
	DoutPrefixProvider *dpp) {
	const uint64_t before_size = hinfo->get_total_logical_size(sinfo);
	ceph_assert(sinfo.logical_offset_is_stripe_aligned(offset));
	ceph_assert(sinfo.logical_offset_is_stripe_aligned(bl.length()));
	ceph_assert(bl.length());

	map<int, bufferlist> buffers;
	int r = ECUtil::encode(
	  sinfo, ecimpl, bl, want, &buffers);
	ceph_assert(r ==0);
	written.insert(offset, bl.length(), bl);

	ldpp_dout(dpp, 20) << __func__ << ": " << oid << " new_size" 
			   << offset + bl.length() << dendl;
	if (offset >= before_size) {
	  ceph_assert(offset == before_size);
	hinfo->append(
	      sinfo.aligned_logical_offset_to_chunk_offset(offset),
	      buffers);
	}
	for (auto &&i : *transactions) {
	  ceph_assert(buffers.count(i.first));
	  if(write_sid.find(i.first) == write_sid.end()) {
	    ldpp_dout(dpp, 20) << __func__ << ": transactions is continue write_sid=" <<write_sid << " i.first="<< i.first <<dendl;
	    continue;
	  }
	bufferlist &enc_bl = buffers[i.first];
	if (offset >= before_size) {
	  i.second.set_alloc_hint(
	coll_t(spg_t(pgid,i.first)),
	ghobject_t(oid, ghobject_t::NO_GEN, i.first),
	0, 0,
	CEPH_OSD_ALLOC_HINT_FLAG_SEQUENTIAL_WRITE |
	CEPH_OSD_ALLOC_HINT_FLAG_APPEND_ONLY);
	}
	  i.second.write(
	  coll_t(spg_t(pgid,i.first)),
  	  ghobject_t(oid,ghobject_t::NO_GEN,i.first),
	  sinfo.logical_to_prev_chunk_offset(offset),
	  enc_bl.length(),
	  enc_bl,
	  flags);
	}
      }

bool ECTransaction::requires_overwrite(
  uint64_t prev_size,
  const PGTransaction::ObjectOperation &op) {
  // special handling for truncates to 0
  if (op.truncate && op.truncate->first == 0)
    return false;
  return op.is_none() &&
    ((!op.buffer_updates.empty() &&
      (op.buffer_updates.begin().get_off() < prev_size)) ||
     (op.truncate &&
      (op.truncate->first < prev_size)));
}

void ECTransaction::generate_transactions(
  WritePlan &plan,
  ErasureCodeInterfaceRef &ecimpl,
  pg_t pgid,
  const ECUtil::stripe_info_t &sinfo,
  const map<hobject_t,extent_map> &partial_extents,
  vector<pg_log_entry_t> &entries,
  map<hobject_t,extent_map> *written_map,
  map<shard_id_t, ObjectStore::Transaction> *transactions,
  set<shard_id_t> &write_sid,
  set<hobject_t> *temp_added,
  set<hobject_t> *temp_removed,
  DoutPrefixProvider *dpp,
  bool &have_append,
  bool osd_ec_zero_opt)  
{
  ceph_assert(written_map);
  ceph_assert(transactions);
  ceph_assert(temp_added);
  ceph_assert(temp_removed);
  ceph_assert(plan.t);
  auto &t = *(plan.t);

  auto &hash_infos = plan.hash_infos;

  map<hobject_t, pg_log_entry_t*> obj_to_log;
  for (auto &&i: entries) {
    obj_to_log.insert(make_pair(i.soid, &i));
  }

  t.safe_create_traverse(
    [&](pair<const hobject_t, PGTransaction::ObjectOperation> &opair) {
      const hobject_t &oid = opair.first;
      auto &op = opair.second;
      auto &obc_map = t.obc_map;
      auto &written = (*written_map)[oid];

      auto iter = obj_to_log.find(oid);
      pg_log_entry_t *entry = iter != obj_to_log.end() ? iter->second : nullptr;

      ObjectContextRef obc;
      auto obiter = t.obc_map.find(oid);
      if (obiter != t.obc_map.end()) {
	obc = obiter->second;
      }
      if (entry) {
	ceph_assert(obc);
      } else {
	ceph_assert(oid.is_temp());
      }

      ECUtil::HashInfoRef hinfo;
      {
	auto iter = hash_infos.find(oid);
	ceph_assert(iter != hash_infos.end());
	hinfo = iter->second;
      }

      if (oid.is_temp()) {
	if (op.is_fresh_object()) {
	  temp_added->insert(oid);
	} else if (op.is_delete()) {
	  temp_removed->insert(oid);
	}
      }

      if (entry &&
	  entry->is_modify() &&
	  op.updated_snaps) {
	bufferlist bl(op.updated_snaps->second.size() * 8 + 8);
	encode(op.updated_snaps->second, bl);
	entry->snaps.swap(bl);
	entry->snaps.reassign_to_mempool(mempool::mempool_osd_pglog);
      }

      ldpp_dout(dpp, 20) << "generate_transactions: "
			 << opair.first
			 << ", current size is "
			 << hinfo->get_total_logical_size(sinfo)
			 << " buffers are "
			 << op.buffer_updates
			 << dendl;
      if (op.truncate) {
	ldpp_dout(dpp, 20) << "generate_transactions: "
			   << " truncate is "
			   << *(op.truncate)
			   << dendl;
      }

      if (entry && op.updated_snaps) {
	entry->mod_desc.update_snaps(op.updated_snaps->first);
      }

      map<string, boost::optional<bufferlist> > xattr_rollback;
      ceph_assert(hinfo);
      bufferlist old_hinfo;
      encode(*hinfo, old_hinfo);
      xattr_rollback[ECUtil::get_hinfo_key()] = old_hinfo;
      
      if (op.is_none() && op.truncate && op.truncate->first == 0) {
	ceph_assert(op.truncate->first == 0);
	ceph_assert(op.truncate->first ==
	       op.truncate->second);
	ceph_assert(entry);
	ceph_assert(obc);
	
	if (op.truncate->first != op.truncate->second) {
	  op.truncate->first = op.truncate->second;
	} else {
	  op.truncate = boost::none;
	}

	op.delete_first = true;
	op.init_type = PGTransaction::ObjectOperation::Init::Create();

	if (obc) {
	  /* We need to reapply all of the cached xattrs.
	     * std::map insert fortunately only writes keys
	     * which don't already exist, so this should do
	     * the right thing. */
	  op.attr_updates.insert(
	    obc->attr_cache.begin(),
	    obc->attr_cache.end());
	}
      }

      if (op.delete_first) {
	/* We also want to remove the boost::none entries since
	   * the keys already won't exist */
	for (auto j = op.attr_updates.begin();
	     j != op.attr_updates.end();
	  ) {
	  if (j->second) {
	    ++j;
	  } else {
	    op.attr_updates.erase(j++);
	  }
	}
	/* Fill in all current entries for xattr rollback */
	if (obc) {
	  xattr_rollback.insert(
	    obc->attr_cache.begin(),
	    obc->attr_cache.end());
	  obc->attr_cache.clear();
	}
	if (entry) {
	  entry->mod_desc.rmobject(entry->version.version);
	  for (auto &&st: *transactions) {
	    st.second.collection_move_rename(
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(oid, ghobject_t::NO_GEN, st.first),
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(oid, entry->version.version, st.first));
	  }
	} else {
	  for (auto &&st: *transactions) {
	    st.second.remove(
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(oid, ghobject_t::NO_GEN, st.first));
	  }
	}
	hinfo->clear();
      }

      if (op.is_fresh_object() && entry) {
	entry->mod_desc.create();
      }

      match(
	op.init_type,
	[&](const PGTransaction::ObjectOperation::Init::None &) {},
	[&](const PGTransaction::ObjectOperation::Init::Create &op) {
	  for (auto &&st: *transactions) {
	    st.second.touch(
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(oid, ghobject_t::NO_GEN, st.first));
	  }
	},
	[&](const PGTransaction::ObjectOperation::Init::Clone &op) {
	  for (auto &&st: *transactions) {
	    st.second.clone(
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(op.source, ghobject_t::NO_GEN, st.first),
	      ghobject_t(oid, ghobject_t::NO_GEN, st.first));
	  }

	  auto siter = hash_infos.find(op.source);
	  ceph_assert(siter != hash_infos.end());
	  hinfo->update_to(*(siter->second));

	  if (obc) {
	    auto cobciter = obc_map.find(op.source);
	    ceph_assert(cobciter != obc_map.end());
	    obc->attr_cache = cobciter->second->attr_cache;
	  }
	},
	[&](const PGTransaction::ObjectOperation::Init::Rename &op) {
	  ceph_assert(op.source.is_temp());
	  for (auto &&st: *transactions) {
	    st.second.collection_move_rename(
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(op.source, ghobject_t::NO_GEN, st.first),
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(oid, ghobject_t::NO_GEN, st.first));
	  }
	  auto siter = hash_infos.find(op.source);
	  ceph_assert(siter != hash_infos.end());
	  hinfo->update_to(*(siter->second));
	  if (obc) {
	    auto cobciter = obc_map.find(op.source);
	    ceph_assert(cobciter == obc_map.end());
	    obc->attr_cache.clear();
	  }
	});

      // omap not supported (except 0, handled above)
      ceph_assert(!(op.clear_omap));
      ceph_assert(!(op.omap_header));
      ceph_assert(op.omap_updates.empty());

      if (!op.attr_updates.empty()) {
	map<string, bufferlist> to_set;
	for (auto &&j: op.attr_updates) {
	  if (j.second) {
	    to_set[j.first] = *(j.second);
	  } else {
	    for (auto &&st : *transactions) {
	      st.second.rmattr(
		coll_t(spg_t(pgid, st.first)),
		ghobject_t(oid, ghobject_t::NO_GEN, st.first),
		j.first);
	    }
	  }
	  if (obc) {
	    auto citer = obc->attr_cache.find(j.first);
	    if (entry) {
	      if (citer != obc->attr_cache.end()) {
		// won't overwrite anything we put in earlier
		xattr_rollback.insert(
		  make_pair(
		    j.first,
		    boost::optional<bufferlist>(citer->second)));
	      } else {
		// won't overwrite anything we put in earlier
		xattr_rollback.insert(
		  make_pair(
		    j.first,
		    boost::none));
	      }
	    }
	    if (j.second) {
	      obc->attr_cache[j.first] = *(j.second);
	    } else if (citer != obc->attr_cache.end()) {
	      obc->attr_cache.erase(citer);
	    }
	  } else {
	    ceph_assert(!entry);
	  }
	}
	for (auto &&st : *transactions) {
	  st.second.setattrs(
	    coll_t(spg_t(pgid, st.first)),
	    ghobject_t(oid, ghobject_t::NO_GEN, st.first),
	    to_set);
	}
	ceph_assert(!xattr_rollback.empty());
      }
      if (entry && !xattr_rollback.empty()) {
	entry->mod_desc.setattrs(xattr_rollback);
      }

      if (op.alloc_hint) {
	/* logical_to_next_chunk_offset() scales down both aligned and
	   * unaligned offsets
	   
	   * we don't bother to roll this back at this time for two reasons:
	   * 1) it's advisory
	   * 2) we don't track the old value */
	uint64_t object_size = sinfo.logical_to_next_chunk_offset(
	  op.alloc_hint->expected_object_size);
	uint64_t write_size = sinfo.logical_to_next_chunk_offset(
	  op.alloc_hint->expected_write_size);
	
	for (auto &&st : *transactions) {
	  st.second.set_alloc_hint(
	    coll_t(spg_t(pgid, st.first)),
	    ghobject_t(oid, ghobject_t::NO_GEN, st.first),
	    object_size,
	    write_size,
	    op.alloc_hint->flags);
	}
      }
      bufferlist olddata;
      extent_map to_write;
      auto pextiter = partial_extents.find(oid);
      if (pextiter != partial_extents.end()) {
	to_write = pextiter->second;
      }
      ldpp_dout(dpp, 20) << __func__ << ": to_write " << to_write << dendl;	
      vector<pair<uint64_t, uint64_t> > rollback_extents;
      const uint64_t orig_size = hinfo->get_total_logical_size(sinfo);

      uint64_t new_size = orig_size;
      uint64_t append_after = new_size;
      ldpp_dout(dpp, 20) << __func__ << ": new_size start " << new_size << dendl;
      map<int, std::pair<uint64_t, uint64_t>> zero_map;
      if (op.truncate && op.truncate->first < new_size) {
	ceph_assert(!op.is_fresh_object());
	new_size = sinfo.logical_to_next_stripe_offset(
	  op.truncate->first);
	ldpp_dout(dpp, 20) << __func__ << ": new_size truncate down "
			   << new_size << dendl;
	if (new_size != op.truncate->first) { // 0 the unaligned part
	  bufferlist bl;
	  bl.append_zero(new_size - op.truncate->first);
	  to_write.insert(
	    op.truncate->first,
	    bl.length(),
	    bl);
	  append_after = sinfo.logical_to_prev_stripe_offset(
	    op.truncate->first);
	} else {
	  append_after = new_size;
	}
	to_write.erase(
	  new_size,
	  std::numeric_limits<uint64_t>::max() - new_size);

	if (entry && !op.is_fresh_object()) {
	  uint64_t restore_from = sinfo.logical_to_prev_chunk_offset(
	    op.truncate->first);
	  uint64_t restore_len = sinfo.aligned_logical_offset_to_chunk_offset(
	    orig_size -
	    sinfo.logical_to_prev_stripe_offset(op.truncate->first));
	  ceph_assert(rollback_extents.empty());

	  ldpp_dout(dpp, 20) << __func__ << ": saving extent "
			     << make_pair(restore_from, restore_len)
			     << dendl;
	  ldpp_dout(dpp, 20) << __func__ << ": truncating to "
			     << new_size
			     << dendl;
	  rollback_extents.emplace_back(
	    make_pair(restore_from, restore_len));
	  for (auto &&st : *transactions) {
	    st.second.touch(
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(oid, entry->version.version, st.first));
	    st.second.clone_range(
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(oid, ghobject_t::NO_GEN, st.first),
	      ghobject_t(oid, entry->version.version, st.first),
	      restore_from,
	      restore_len,
	      restore_from);
	    
	  }
	} else {
	  ldpp_dout(dpp, 20) << __func__ << ": not saving extents, fresh object"
			     << dendl;
	}
	for (auto &&st : *transactions) {
	  st.second.truncate(
	    coll_t(spg_t(pgid, st.first)),
	    ghobject_t(oid, ghobject_t::NO_GEN, st.first),
	    sinfo.aligned_logical_offset_to_chunk_offset(new_size));
	}
      }
      ldpp_dout(dpp, 20) << __func__ << ": to_write " << to_write << dendl;
      uint32_t fadvise_flags = 0;
      for (auto &&extent: op.buffer_updates) {
	using BufferUpdate = PGTransaction::ObjectOperation::BufferUpdate;
	bufferlist bl;
	match(
	  extent.get_val(),
	  [&](const BufferUpdate::Write &op) {
	    bl = op.buffer;
	    fadvise_flags |= op.fadvise_flags;
	  },
	  [&](const BufferUpdate::Zero &) {
	    bl.append_zero(extent.get_len());
	  },
	  [&](const BufferUpdate::CloneRange &) {
	    ceph_assert(
	      0 ==
	      "CloneRange is not allowed, do_op should have returned ENOTSUPP");
	  });

	uint64_t off = extent.get_off();
	uint64_t len = extent.get_len();
	uint64_t end = off + len;
	ldpp_dout(dpp, 20) << __func__ << ": adding buffer_update "
			   << make_pair(off, len)
			   << " bl length: " << bl.length()
			   << dendl;
	ceph_assert(len > 0);
	if (off > new_size) {
	  ceph_assert(off > append_after);
	  bl.prepend_zero(off - new_size);
	  len += off - new_size;
	  ldpp_dout(dpp, 20) << __func__ << ": prepending zeroes to align "
			     << off << "->" << new_size
			     << dendl;
	  off = new_size;
	}
	if (!sinfo.logical_offset_is_stripe_aligned(end) && (end > append_after)) {
	  uint64_t aligned_end = sinfo.logical_to_next_stripe_offset(end);
	  uint64_t tail = aligned_end - end;
	  if (osd_ec_zero_opt && append_after < aligned_end) {
	   HiEcInfo ec_info(ecimpl->get_data_chunk_count(),ecimpl->get_chunk_count(),
			    ecimpl->get_chunk_mapping(), sinfo.get_chunk_size(),\
			    sinfo.get_stripe_width(),sinfo.get_chunk_unit_size());
	   HiGetShardZeroRange(std::pair(append_after, end - append_after), ec_info, zero_map);
	  }
	  bl.append_zero(tail);
	  ldpp_dout(dpp, 20) << __func__ << ": appending zeroes to align end "
			     << end << "->" << end+tail
			     << ", len: " << len << "->" << len+tail
			     << dendl;
	  end += tail;
	  len += tail;
	}
        ldpp_dout(dpp, 20) << __func__ << ": adding buffer_update " <<dendl;
	if (write_sid.empty()) {
	  pair<uint64_t,uint64_t> tmp = sinfo.offset_len_to_chunk_unit_bounds(make_pair(off, len));
	  auto range = to_write.get_containing_range(tmp.first, tmp.second);
	  ldpp_dout(dpp, 20) << __func__ << "chunk aligned off" << tmp.first << " len " << tmp.second
 	    <<" range len " << range.first.get_val().length() << dendl;
	    olddata.substr_of(range.first.get_val(), tmp.first - range.first.get_off(), tmp.second);
	}
	ldpp_dout(dpp, 20) << __func__ <<": adding buffer_update "<<dendl;
	ldpp_dout(dpp, 20) << __func__ <<": to_write= " << to_write<< "off=" <<off <<" len=" <<len<< " bl="<<bl<<dendl;
	to_write.insert(off, len, bl);
	ldpp_dout(dpp, 20) << __func__ <<": after insert to_write= " << to_write<< " bl="<<bl<<dendl;
	if (end > new_size)
	  new_size = end;
      }
	ldpp_dout(dpp, 20) << __func__ <<": to_write " << to_write<<dendl;
      if (op.truncate &&
	  op.truncate->second > new_size) {
	ceph_assert(op.truncate->second > append_after);
	uint64_t truncate_to =
	  sinfo.logical_to_next_stripe_offset(
	    op.truncate->second);
	uint64_t zeroes = truncate_to - new_size;
	bufferlist bl;
	bl.append_zero(zeroes);
	to_write.insert(
	  new_size,
	  zeroes,
	  bl);
	new_size = truncate_to;
	ldpp_dout(dpp, 20) << __func__ << ": truncating out to "
			   << truncate_to
			   << dendl;
      }

      set<int> want;
      for (unsigned i = 0; i < ecimpl->get_chunk_count(); ++i) {
	want.insert(i);
      }
      auto to_overwrite = to_write.intersect(0, append_after);
      ldpp_dout(dpp, 20) << __func__ << ": to_overwrite: "
			 << to_overwrite
			 << dendl;
      for (auto &&extent: to_overwrite) {
	uint64_t off = extent.get_off();
	uint64_t len = extent.get_len();
	if (write_sid.empty()) {
	  pair<uint64_t, uint64_t> tmp = sinfo.offset_len_to_stripe_bounds(make_pair(off,len));
	  off = tmp.first;
	  len = tmp.second;
	}
	ceph_assert(off + len <= append_after);
	ceph_assert(sinfo.logical_offset_is_stripe_aligned(off));
	ceph_assert(sinfo.logical_offset_is_stripe_aligned(len));
	if (entry) {
	  uint64_t restore_from = sinfo.aligned_logical_offset_to_chunk_offset(
	    off);
	  uint64_t restore_len = sinfo.aligned_logical_offset_to_chunk_offset(
	    len);
	  ldpp_dout(dpp, 20) << __func__ << ": overwriting section"
			     << restore_from << "~" << restore_len
			     << dendl;
	  if (rollback_extents.empty()) {
	    for (auto &&st : *transactions) {
	      st.second.touch(
		coll_t(spg_t(pgid, st.first)),
		ghobject_t(oid, entry->version.version, st.first));
	    }
	  }
	  rollback_extents.emplace_back(make_pair(restore_from, restore_len));
	  for (auto &&st : *transactions) {
	    st.second.clone_range(
	      coll_t(spg_t(pgid, st.first)),
	      ghobject_t(oid, ghobject_t::NO_GEN, st.first),
	      ghobject_t(oid, entry->version.version, st.first),
	      restore_from,
	      restore_len,
	      restore_from);
	  }
	}
	ldpp_dout(dpp, 20) << __func__ << " before encode and write write_sid:" << write_sid << dendl;
	if (!write_sid.empty()) {
	encode_and_write_append(
	  pgid,
	  oid,
	  sinfo,
	  ecimpl,
	  want,
	  extent.get_off(),
	  extent.get_val(),
	  fadvise_flags,
	  hinfo,
	  written,
	  transactions,
	  write_sid,
	  dpp);
	} else {
          encode_and_write_update(
	  pgid,
	  oid,
	  sinfo,
	  ecimpl,
	  extent.get_off(),
	  olddata,
	  extent.get_val(),
	  fadvise_flags,
	  hinfo,
	  written,
	  transactions,
	  dpp);
	}
      }
      auto to_append = to_write.intersect(
	append_after,
	std::numeric_limits<uint64_t>::max() - append_after);
      ldpp_dout(dpp, 20) << __func__ << ": to_append: "
			 << to_append
			 << dendl;
      for (auto &&extent: to_append) {
	ceph_assert(sinfo.logical_offset_is_stripe_aligned(extent.get_off()));
	ceph_assert(sinfo.logical_offset_is_stripe_aligned(extent.get_len()));
	ldpp_dout(dpp, 20) << __func__ << ": appending section "
			   << extent.get_off() << "~" << extent.get_len()
			   << dendl;
	encode_and_write(
	  pgid,
	  oid,
	  sinfo,
	  ecimpl,
	  want,
	  extent.get_off(),
	  extent.get_val(),
	  fadvise_flags,
	  hinfo,
	  written,
	  transactions,
	  zero_map,
	  dpp);
      }

      ldpp_dout(dpp, 20) << __func__ << ": " << oid
			 << " resetting hinfo to logical size "
			 << new_size
			 << dendl;
      if (!rollback_extents.empty() && entry) {
	if (entry) {
	  ldpp_dout(dpp, 20) << __func__ << ": " << oid
			     << " marking rollback extents "
			     << rollback_extents
			     << dendl;
	  entry->mod_desc.rollback_extents(
	    entry->version.version, rollback_extents);
	}
	hinfo->set_total_chunk_size_clear_hash(
	  sinfo.aligned_logical_offset_to_chunk_offset(new_size));
      } else {
	ceph_assert(hinfo->get_total_logical_size(sinfo) == new_size);
      }

      if (entry && !to_append.empty()) {
	ldpp_dout(dpp, 20) << __func__ << ": marking append "
			   << append_after
			   << dendl;
	entry->mod_desc.append(append_after);
	have_append = true;
      }

      if (!op.is_delete()) {
	bufferlist hbuf;
	encode(*hinfo, hbuf);
	for (auto &&i : *transactions) {
	  i.second.setattr(
	    coll_t(spg_t(pgid, i.first)),
	    ghobject_t(oid, ghobject_t::NO_GEN, i.first),
	    ECUtil::get_hinfo_key(),
	    hbuf);
	}
      }
    });
}
