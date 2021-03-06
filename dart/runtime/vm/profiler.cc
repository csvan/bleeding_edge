// Copyright (c) 2013, the Dart project authors.  Please see the AUTHORS file
// for details. All rights reserved. Use of this source code is governed by a
// BSD-style license that can be found in the LICENSE file.

#include "platform/utils.h"

#include "vm/allocation.h"
#include "vm/atomic.h"
#include "vm/code_patcher.h"
#include "vm/isolate.h"
#include "vm/json_stream.h"
#include "vm/native_symbol.h"
#include "vm/object.h"
#include "vm/os.h"
#include "vm/profiler.h"
#include "vm/signal_handler.h"
#include "vm/simulator.h"
#include "vm/stack_frame.h"

namespace dart {


#if defined(USING_SIMULATOR) || defined(TARGET_OS_WINDOWS) || \
    defined(TARGET_OS_ANDROID)
  DEFINE_FLAG(bool, profile, false, "Enable Sampling Profiler");
#else
  DEFINE_FLAG(bool, profile, true, "Enable Sampling Profiler");
#endif
DEFINE_FLAG(bool, trace_profiled_isolates, false, "Trace profiled isolates.");
DEFINE_FLAG(charp, profile_dir, NULL,
            "Enable writing profile data into specified directory.");
DEFINE_FLAG(int, profile_period, 1000,
            "Time between profiler samples in microseconds. Minimum 250.");
DEFINE_FLAG(int, profile_depth, 8,
            "Maximum number stack frames walked. Minimum 1. Maximum 255.");
DEFINE_FLAG(bool, profile_verify_stack_walk, false,
            "Verify instruction addresses while walking the stack.");

bool Profiler::initialized_ = false;
SampleBuffer* Profiler::sample_buffer_ = NULL;

void Profiler::InitOnce() {
  // Place some sane restrictions on user controlled flags.
  SetSamplePeriod(FLAG_profile_period);
  SetSampleDepth(FLAG_profile_depth);
  if (!FLAG_profile) {
    return;
  }
  ASSERT(!initialized_);
  sample_buffer_ = new SampleBuffer();
  NativeSymbolResolver::InitOnce();
  ThreadInterrupter::SetInterruptPeriod(FLAG_profile_period);
  ThreadInterrupter::Startup();
  initialized_ = true;
}


void Profiler::Shutdown() {
  if (!FLAG_profile) {
    return;
  }
  ASSERT(initialized_);
  ThreadInterrupter::Shutdown();
  NativeSymbolResolver::ShutdownOnce();
}


void Profiler::SetSampleDepth(intptr_t depth) {
  const int kMinimumDepth = 1;
  const int kMaximumDepth = kSampleFramesSize - 1;
  if (depth < kMinimumDepth) {
    FLAG_profile_depth = kMinimumDepth;
  } else if (depth > kMaximumDepth) {
    FLAG_profile_depth = kMaximumDepth;
  } else {
    FLAG_profile_depth = depth;
  }
}


void Profiler::SetSamplePeriod(intptr_t period) {
  const int kMinimumProfilePeriod = 250;
  if (period < kMinimumProfilePeriod) {
    FLAG_profile_period = kMinimumProfilePeriod;
  } else {
    FLAG_profile_period = period;
  }
}


void Profiler::InitProfilingForIsolate(Isolate* isolate, bool shared_buffer) {
  if (!FLAG_profile) {
    return;
  }
  ASSERT(isolate != NULL);
  ASSERT(sample_buffer_ != NULL);
  {
    MutexLocker profiler_data_lock(isolate->profiler_data_mutex());
    SampleBuffer* sample_buffer = sample_buffer_;
    if (!shared_buffer) {
      sample_buffer = new SampleBuffer();
    }
    IsolateProfilerData* profiler_data =
        new IsolateProfilerData(sample_buffer, !shared_buffer);
    ASSERT(profiler_data != NULL);
    isolate->set_profiler_data(profiler_data);
    if (FLAG_trace_profiled_isolates) {
      OS::Print("Profiler Setup %p %s\n", isolate, isolate->name());
    }
  }
}


void Profiler::ShutdownProfilingForIsolate(Isolate* isolate) {
  ASSERT(isolate != NULL);
  if (!FLAG_profile) {
    return;
  }
  // We do not have a current isolate.
  ASSERT(Isolate::Current() == NULL);
  {
    MutexLocker profiler_data_lock(isolate->profiler_data_mutex());
    IsolateProfilerData* profiler_data = isolate->profiler_data();
    if (profiler_data == NULL) {
      // Already freed.
      return;
    }
    isolate->set_profiler_data(NULL);
    delete profiler_data;
    if (FLAG_trace_profiled_isolates) {
      OS::Print("Profiler Shutdown %p %s\n", isolate, isolate->name());
    }
  }
}


void Profiler::BeginExecution(Isolate* isolate) {
  if (isolate == NULL) {
    return;
  }
  if (!FLAG_profile) {
    return;
  }
  ASSERT(initialized_);
  IsolateProfilerData* profiler_data = isolate->profiler_data();
  if (profiler_data == NULL) {
    return;
  }
  ThreadInterrupter::Register(RecordSampleInterruptCallback, isolate);
}


void Profiler::EndExecution(Isolate* isolate) {
  if (isolate == NULL) {
    return;
  }
  if (!FLAG_profile) {
    return;
  }
  ASSERT(initialized_);
  ThreadInterrupter::Unregister();
}


class ScopeStopwatch : public ValueObject {
 public:
  explicit ScopeStopwatch(const char* name) : name_(name) {
    start_ = FLAG_trace_profiled_isolates ? OS::GetCurrentTimeMillis() : 0;
  }

  intptr_t GetElapsed() const {
    intptr_t end = OS::GetCurrentTimeMillis();
    ASSERT(end >= start_);
    return end - start_;
  }

  ~ScopeStopwatch() {
    if (FLAG_trace_profiled_isolates) {
      intptr_t elapsed = GetElapsed();
      OS::Print("%s took %" Pd " millis.\n", name_, elapsed);
    }
  }

 private:
  const char* name_;
  intptr_t start_;
};


struct AddressEntry {
  uword pc;
  intptr_t exclusive_ticks;
  intptr_t inclusive_ticks;

  void tick(bool exclusive) {
    if (exclusive) {
      exclusive_ticks++;
    } else {
      inclusive_ticks++;
    }
  }
};


struct CallEntry {
  intptr_t code_table_index;
  intptr_t count;
};


typedef bool (*RegionCompare)(uword pc, uword region_start, uword region_end);


class CodeRegionTrieNode : public ZoneAllocated {
 public:
  explicit CodeRegionTrieNode(intptr_t code_region_index)
      : code_region_index_(code_region_index),
        count_(0),
        children_(new ZoneGrowableArray<CodeRegionTrieNode*>()) {
  }

  void Tick() {
    ASSERT(code_region_index_ >= 0);
    count_++;
  }

  intptr_t count() const {
    ASSERT(code_region_index_ >= 0);
    return count_;
  }

  intptr_t code_region_index() const {
    return code_region_index_;
  }

  ZoneGrowableArray<CodeRegionTrieNode*>& children() const {
    return *children_;
  }

  CodeRegionTrieNode* GetChild(intptr_t child_code_region_index) {
    const intptr_t length = children_->length();
    intptr_t i = 0;
    while (i < length) {
      CodeRegionTrieNode* child = (*children_)[i];
      if (child->code_region_index() == child_code_region_index) {
        return child;
      }
      if (child->code_region_index() > child_code_region_index) {
        break;
      }
      i++;
    }
    // Add new CodeRegion, sorted by CodeRegionTable index.
    CodeRegionTrieNode* child = new CodeRegionTrieNode(child_code_region_index);
    if (i < length) {
      // Insert at i.
      children_->InsertAt(i, child);
    } else {
      // Add to end.
      children_->Add(child);
    }
    return child;
  }

  // Sort this's children and (recursively) all descendants by count.
  // This should only be called after the trie is completely built.
  void SortByCount() {
    children_->Sort(CodeRegionTrieNodeCompare);
    ZoneGrowableArray<CodeRegionTrieNode*>& kids = children();
    intptr_t child_count = kids.length();
    // Recurse.
    for (intptr_t i = 0; i < child_count; i++) {
      kids[i]->SortByCount();
    }
  }

  void PrintToJSONArray(JSONArray* array) const {
    ASSERT(array != NULL);
    // Write CodeRegion index.
    array->AddValue(code_region_index_);
    // Write count.
    array->AddValue(count_);
    // Write number of children.
    ZoneGrowableArray<CodeRegionTrieNode*>& kids = children();
    intptr_t child_count = kids.length();
    array->AddValue(child_count);
    // Recurse.
    for (intptr_t i = 0; i < child_count; i++) {
      kids[i]->PrintToJSONArray(array);
    }
  }

 private:
  static int CodeRegionTrieNodeCompare(CodeRegionTrieNode* const* a,
                                       CodeRegionTrieNode* const* b) {
    ASSERT(a != NULL);
    ASSERT(b != NULL);
    return (*b)->count() - (*a)->count();
  }

  const intptr_t code_region_index_;
  intptr_t count_;
  ZoneGrowableArray<CodeRegionTrieNode*>* children_;
};


// A contiguous address region that holds code. Each CodeRegion has a "kind"
// which describes the type of code contained inside the region. Each
// region covers the following interval: [start, end).
class CodeRegion : public ZoneAllocated {
 public:
  enum Kind {
    kDartCode,       // Live Dart code.
    kCollectedCode,  // Dead Dart code.
    kNativeCode,     // Native code.
    kReusedCode,     // Dead Dart code that has been reused by new kDartCode.
    kTagCode,        // A special kind of code representing a tag.
  };

  CodeRegion(Kind kind, uword start, uword end, int64_t timestamp)
      : kind_(kind),
        start_(start),
        end_(end),
        inclusive_ticks_(0),
        exclusive_ticks_(0),
        inclusive_tick_serial_(0),
        name_(NULL),
        compile_timestamp_(timestamp),
        creation_serial_(0),
        address_table_(new ZoneGrowableArray<AddressEntry>()),
        callers_table_(new ZoneGrowableArray<CallEntry>()),
        callees_table_(new ZoneGrowableArray<CallEntry>()) {
    ASSERT(start_ < end_);
  }


  uword start() const { return start_; }
  void set_start(uword start) {
    start_ = start;
  }

  uword end() const { return end_; }
  void set_end(uword end) {
    end_ = end;
  }

  void AdjustExtent(uword start, uword end) {
    if (start < start_) {
      start_ = start;
    }
    if (end > end_) {
      end_ = end;
    }
    ASSERT(start_ < end_);
  }

  bool contains(uword pc) const {
    return (pc >= start_) && (pc < end_);
  }

  bool overlaps(const CodeRegion* other) const {
    ASSERT(other != NULL);
    return other->contains(start_)   ||
           other->contains(end_ - 1) ||
           contains(other->start())  ||
           contains(other->end() - 1);
  }

  intptr_t creation_serial() const { return creation_serial_; }
  void set_creation_serial(intptr_t serial) {
    creation_serial_ = serial;
  }
  int64_t compile_timestamp() const { return compile_timestamp_; }
  void set_compile_timestamp(int64_t timestamp) {
    compile_timestamp_ = timestamp;
  }

  intptr_t inclusive_ticks() const { return inclusive_ticks_; }
  void set_inclusive_ticks(intptr_t inclusive_ticks) {
    inclusive_ticks_ = inclusive_ticks;
  }

  intptr_t exclusive_ticks() const { return exclusive_ticks_; }
  void set_exclusive_ticks(intptr_t exclusive_ticks) {
    exclusive_ticks_ = exclusive_ticks;
  }

  const char* name() const { return name_; }
  void SetName(const char* name) {
    if (name == NULL) {
      name_ = NULL;
    }
    intptr_t len = strlen(name);
    name_ = Isolate::Current()->current_zone()->Alloc<const char>(len + 1);
    strncpy(const_cast<char*>(name_), name, len);
    const_cast<char*>(name_)[len] = '\0';
  }

  Kind kind() const { return kind_; }

  static const char* KindToCString(Kind kind) {
    switch (kind) {
      case kDartCode:
        return "Dart";
      case kCollectedCode:
        return "Collected";
      case kNativeCode:
        return "Native";
      case kReusedCode:
        return "Overwritten";
      case kTagCode:
        return "Tag";
    }
    UNREACHABLE();
    return NULL;
  }

  void DebugPrint() const {
    OS::Print("%s [%" Px ", %" Px ") %" Pd " %" Pd64 "\n",
              KindToCString(kind_),
              start(),
              end(),
              creation_serial_,
              compile_timestamp_);
  }

  void Tick(uword pc, bool exclusive, intptr_t serial) {
    // Assert that exclusive ticks are never passed a valid serial number.
    ASSERT((exclusive && (serial == -1)) || (!exclusive && (serial != -1)));
    if (!exclusive && (inclusive_tick_serial_ == serial)) {
      // We've already given this code object an inclusive tick for this sample.
      return;
    }
    // Tick the code object.
    if (exclusive) {
      exclusive_ticks_++;
    } else {
      inclusive_ticks_++;
      // Mark the last serial we ticked the inclusive count.
      inclusive_tick_serial_ = serial;
    }
    TickAddress(pc, exclusive);
  }

  void AddCaller(intptr_t index, intptr_t count) {
    AddCallEntry(callers_table_, index, count);
  }

  void AddCallee(intptr_t index, intptr_t count) {
    AddCallEntry(callees_table_, index, count);
  }

  void PrintNativeCode(JSONObject* profile_code_obj) {
    ASSERT(kind() == kNativeCode);
    JSONObject obj(profile_code_obj, "code");
    obj.AddProperty("type", "@Code");
    obj.AddProperty("kind", "Native");
    obj.AddProperty("name", name());
    obj.AddProperty("user_name", name());
    obj.AddPropertyF("start", "%" Px "", start());
    obj.AddPropertyF("end", "%" Px "", end());
    obj.AddPropertyF("id", "code/native-%" Px "", start());
    {
      // Generate a fake function entry.
      JSONObject func(&obj, "function");
      func.AddProperty("type", "@Function");
      func.AddPropertyF("id", "functions/native-%" Px "", start());
      func.AddProperty("name", name());
      func.AddProperty("user_name", name());
      func.AddProperty("kind", "Native");
    }
  }

  void PrintCollectedCode(JSONObject* profile_code_obj) {
    ASSERT(kind() == kCollectedCode);
    JSONObject obj(profile_code_obj, "code");
    obj.AddProperty("type", "@Code");
    obj.AddProperty("kind", "Collected");
    obj.AddProperty("name", name());
    obj.AddProperty("user_name", name());
    obj.AddPropertyF("start", "%" Px "", start());
    obj.AddPropertyF("end", "%" Px "", end());
    obj.AddPropertyF("id", "code/collected-%" Px "", start());
    {
      // Generate a fake function entry.
      JSONObject func(&obj, "function");
      func.AddProperty("type", "@Function");
      obj.AddPropertyF("id", "functions/collected-%" Px "", start());
      func.AddProperty("name", name());
      func.AddProperty("user_name", name());
      func.AddProperty("kind", "Collected");
    }
  }

  void PrintOverwrittenCode(JSONObject* profile_code_obj) {
    ASSERT(kind() == kReusedCode);
    JSONObject obj(profile_code_obj, "code");
    obj.AddProperty("type", "@Code");
    obj.AddProperty("kind", "Reused");
    obj.AddProperty("name", name());
    obj.AddProperty("user_name", name());
    obj.AddPropertyF("start", "%" Px "", start());
    obj.AddPropertyF("end", "%" Px "", end());
    obj.AddPropertyF("id", "code/reused-%" Px "", start());
    {
      // Generate a fake function entry.
      JSONObject func(&obj, "function");
      func.AddProperty("type", "@Function");
      obj.AddPropertyF("id", "functions/reused-%" Px "", start());
      func.AddProperty("name", name());
      func.AddProperty("user_name", name());
      func.AddProperty("kind", "Reused");
    }
  }

  void  PrintTagCode(JSONObject* profile_code_obj) {
    ASSERT(kind() == kTagCode);
    JSONObject obj(profile_code_obj, "code");
    obj.AddProperty("type", "@Code");
    obj.AddProperty("kind", "Tag");
    obj.AddPropertyF("id", "code/tag-%" Px "", start());
    obj.AddProperty("name", name());
    obj.AddProperty("user_name", name());
    obj.AddPropertyF("start", "%" Px "", start());
    obj.AddPropertyF("end", "%" Px "", end());
    {
      // Generate a fake function entry.
      JSONObject func(&obj, "function");
      func.AddProperty("type", "@Function");
      func.AddProperty("kind", "Tag");
      obj.AddPropertyF("id", "functions/tag-%" Px "", start());
      func.AddProperty("name", name());
      func.AddProperty("user_name", name());
    }
  }

  void PrintToJSONArray(Isolate* isolate, JSONArray* events, bool full) {
    JSONObject obj(events);
    obj.AddProperty("type", "CodeRegion");
    obj.AddProperty("kind", KindToCString(kind()));
    obj.AddPropertyF("inclusive_ticks", "%" Pd "", inclusive_ticks());
    obj.AddPropertyF("exclusive_ticks", "%" Pd "", exclusive_ticks());
    if (kind() == kDartCode) {
      // Look up code in Dart heap.
      Code& code = Code::Handle(isolate);
      code ^= Code::LookupCode(start());
      if (code.IsNull()) {
        // Code is a stub in the Vm isolate.
        code ^= Code::LookupCodeInVmIsolate(start());
      }
      ASSERT(!code.IsNull());
      obj.AddProperty("code", code, !full);
    } else if (kind() == kCollectedCode) {
      if (name() == NULL) {
        // Lazily set generated name.
        GenerateAndSetSymbolName("[Collected]");
      }
      PrintCollectedCode(&obj);
    } else if (kind() == kReusedCode) {
      if (name() == NULL) {
        // Lazily set generated name.
        GenerateAndSetSymbolName("[Reused]");
      }
      PrintOverwrittenCode(&obj);
    } else if (kind() == kTagCode) {
      if (name() == NULL) {
        const char* tag_name = start() == 0 ? "root" : VMTag::TagName(start());
        ASSERT(tag_name != NULL);
        SetName(tag_name);
      }
      PrintTagCode(&obj);
    } else {
      ASSERT(kind() == kNativeCode);
      if (name() == NULL) {
        // Lazily set generated name.
        GenerateAndSetSymbolName("[Native]");
      }
      PrintNativeCode(&obj);
    }
    {
      JSONArray ticks(&obj, "ticks");
      for (intptr_t i = 0; i < address_table_->length(); i++) {
        const AddressEntry& entry = (*address_table_)[i];
        ticks.AddValueF("%" Px "", entry.pc);
        ticks.AddValueF("%" Pd "", entry.exclusive_ticks);
        ticks.AddValueF("%" Pd "", entry.inclusive_ticks);
      }
    }
    {
      JSONArray callers(&obj, "callers");
      for (intptr_t i = 0; i < callers_table_->length(); i++) {
        const CallEntry& entry = (*callers_table_)[i];
        callers.AddValueF("%" Pd "", entry.code_table_index);
        callers.AddValueF("%" Pd "", entry.count);
      }
    }
    {
      JSONArray callees(&obj, "callees");
      for (intptr_t i = 0; i < callees_table_->length(); i++) {
        const CallEntry& entry = (*callees_table_)[i];
        callees.AddValueF("%" Pd "", entry.code_table_index);
        callees.AddValueF("%" Pd "", entry.count);
      }
    }
  }

 private:
  void TickAddress(uword pc, bool exclusive) {
    const intptr_t length = address_table_->length();
    intptr_t i = 0;
    for (; i < length; i++) {
      AddressEntry& entry = (*address_table_)[i];
      if (entry.pc == pc) {
        // Tick the address entry.
        entry.tick(exclusive);
        return;
      }
      if (entry.pc > pc) {
        break;
      }
    }
    // New address, add entry.
    AddressEntry entry;
    entry.pc = pc;
    entry.exclusive_ticks = 0;
    entry.inclusive_ticks = 0;
    entry.tick(exclusive);
    if (i < length) {
      // Insert at i.
      address_table_->InsertAt(i, entry);
    } else {
      // Add to end.
      address_table_->Add(entry);
    }
  }


  void AddCallEntry(ZoneGrowableArray<CallEntry>* table, intptr_t index,
                    intptr_t count) {
    const intptr_t length = table->length();
    intptr_t i = 0;
    for (; i < length; i++) {
      CallEntry& entry = (*table)[i];
      if (entry.code_table_index == index) {
        entry.count += count;
        return;
      }
      if (entry.code_table_index > index) {
        break;
      }
    }
    CallEntry entry;
    entry.code_table_index = index;
    entry.count = count;
    if (i < length) {
      table->InsertAt(i, entry);
    } else {
      table->Add(entry);
    }
  }

  void GenerateAndSetSymbolName(const char* prefix) {
    const intptr_t kBuffSize = 512;
    char buff[kBuffSize];
    OS::SNPrint(&buff[0], kBuffSize-1, "%s [%" Px ", %" Px ")",
                prefix, start(), end());
    SetName(buff);
  }

  // CodeRegion kind.
  const Kind kind_;
  // CodeRegion start address.
  uword start_;
  // CodeRegion end address.
  uword end_;
  // Inclusive ticks.
  intptr_t inclusive_ticks_;
  // Exclusive ticks.
  intptr_t exclusive_ticks_;
  // Inclusive tick serial number, ensures that each CodeRegion is only given
  // a single inclusive tick per sample.
  intptr_t inclusive_tick_serial_;
  // Name of code region.
  const char* name_;
  // The compilation timestamp associated with this code region.
  int64_t compile_timestamp_;
  // Serial number at which this CodeRegion was created.
  intptr_t creation_serial_;
  ZoneGrowableArray<AddressEntry>* address_table_;
  ZoneGrowableArray<CallEntry>* callers_table_;
  ZoneGrowableArray<CallEntry>* callees_table_;
  DISALLOW_COPY_AND_ASSIGN(CodeRegion);
};


// A sorted table of CodeRegions. Does not allow for overlap.
class CodeRegionTable : public ValueObject {
 public:
  enum TickResult {
    kTicked = 0,     // CodeRegion found and ticked.
    kNotFound = -1,   // No CodeRegion found.
    kNewerCode = -2,  // CodeRegion found but it was compiled after sample.
  };

  CodeRegionTable() :
      code_region_table_(new ZoneGrowableArray<CodeRegion*>(64)) {
  }

  // Ticks the CodeRegion containing pc if it is alive at timestamp.
  TickResult Tick(uword pc, bool exclusive, intptr_t serial,
                  int64_t timestamp) {
    intptr_t index = FindIndex(pc);
    if (index < 0) {
      // Not found.
      return kNotFound;
    }
    ASSERT(index < code_region_table_->length());
    CodeRegion* region = At(index);
    if (region->compile_timestamp() > timestamp) {
      // Compiled after tick.
      return kNewerCode;
    }
    region->Tick(pc, exclusive, serial);
    return kTicked;
  }

  // Table length.
  intptr_t Length() const { return code_region_table_->length(); }

  // Get the CodeRegion at index.
  CodeRegion* At(intptr_t index) const {
    return (*code_region_table_)[index];
  }

  // Find the table index to the CodeRegion containing pc.
  // Returns < 0 if not found.
  intptr_t FindIndex(uword pc) const {
    intptr_t index = FindRegionIndex(pc, &CompareLowerBound);
    const CodeRegion* code_region = NULL;
    if (index == code_region_table_->length()) {
      // Not present.
      return -1;
    }
    code_region = At(index);
    if (code_region->contains(pc)) {
      // Found at index.
      return index;
    }
    return -2;
  }

  // Insert code_region into the table. Returns the table index where the
  // CodeRegion was inserted. Will merge with an overlapping CodeRegion if
  // one is present.
  intptr_t InsertCodeRegion(CodeRegion* code_region) {
    const uword start = code_region->start();
    const uword end = code_region->end();
    const intptr_t length = code_region_table_->length();
    if (length == 0) {
      code_region_table_->Add(code_region);
      return length;
    }
    // Determine the correct place to insert or merge code_region into table.
    intptr_t lo = FindRegionIndex(start, &CompareLowerBound);
    intptr_t hi = FindRegionIndex(end - 1, &CompareUpperBound);
    // TODO(johnmccutchan): Simplify below logic.
    if ((lo == length) && (hi == length)) {
      lo = length - 1;
    }
    if (lo == length) {
      CodeRegion* region = At(hi);
      if (region->overlaps(code_region)) {
        HandleOverlap(region, code_region, start, end);
        return hi;
      }
      code_region_table_->Add(code_region);
      return length;
    } else if (hi == length) {
      CodeRegion* region = At(lo);
      if (region->overlaps(code_region)) {
        HandleOverlap(region, code_region, start, end);
        return lo;
      }
      code_region_table_->Add(code_region);
      return length;
    } else if (lo == hi) {
      CodeRegion* region = At(lo);
      if (region->overlaps(code_region)) {
        HandleOverlap(region, code_region, start, end);
        return lo;
      }
      code_region_table_->InsertAt(lo, code_region);
      return lo;
    } else {
      CodeRegion* region = At(lo);
      if (region->overlaps(code_region)) {
        HandleOverlap(region, code_region, start, end);
        return lo;
      }
      region = At(hi);
      if (region->overlaps(code_region)) {
        HandleOverlap(region, code_region, start, end);
        return hi;
      }
      code_region_table_->InsertAt(hi, code_region);
      return hi;
    }
    UNREACHABLE();
  }

#if defined(DEBUG)
  void Verify() {
    VerifyOrder();
    VerifyOverlap();
  }
#endif

  void DebugPrint() {
    OS::Print("Dumping CodeRegionTable:\n");
    for (intptr_t i = 0; i < code_region_table_->length(); i++) {
      CodeRegion* region = At(i);
      region->DebugPrint();
    }
  }

 private:
  intptr_t FindRegionIndex(uword pc, RegionCompare comparator) const {
    ASSERT(comparator != NULL);
    intptr_t count = code_region_table_->length();
    intptr_t first = 0;
    while (count > 0) {
      intptr_t it = first;
      intptr_t step = count / 2;
      it += step;
      const CodeRegion* code_region = At(it);
      if (comparator(pc, code_region->start(), code_region->end())) {
        first = ++it;
        count -= (step + 1);
      } else {
        count = step;
      }
    }
    return first;
  }

  static bool CompareUpperBound(uword pc, uword start, uword end) {
    return pc >= end;
  }

  static bool CompareLowerBound(uword pc, uword start, uword end) {
    return end <= pc;
  }

  void HandleOverlap(CodeRegion* region, CodeRegion* code_region,
                     uword start, uword end) {
    // We should never see overlapping Dart code regions.
    ASSERT(region->kind() != CodeRegion::kDartCode);
    // We should never see overlapping Tag code regions.
    ASSERT(region->kind() != CodeRegion::kTagCode);
    // When code regions overlap, they should be of the same kind.
    ASSERT(region->kind() == code_region->kind());
    region->AdjustExtent(start, end);
  }

#if defined(DEBUG)
  void VerifyOrder() {
    const intptr_t length = code_region_table_->length();
    if (length == 0) {
      return;
    }
    uword last = (*code_region_table_)[0]->end();
    for (intptr_t i = 1; i < length; i++) {
      CodeRegion* a = (*code_region_table_)[i];
      ASSERT(last <= a->start());
      last = a->end();
    }
  }

  void VerifyOverlap() {
    const intptr_t length = code_region_table_->length();
    for (intptr_t i = 0; i < length; i++) {
      CodeRegion* a = (*code_region_table_)[i];
      for (intptr_t j = i+1; j < length; j++) {
        CodeRegion* b = (*code_region_table_)[j];
        ASSERT(!a->contains(b->start()) &&
               !a->contains(b->end() - 1) &&
               !b->contains(a->start()) &&
               !b->contains(a->end() - 1));
      }
    }
  }
#endif

  ZoneGrowableArray<CodeRegion*>* code_region_table_;
};


class CodeRegionTableBuilder : public SampleVisitor {
 public:
  CodeRegionTableBuilder(Isolate* isolate,
                         CodeRegionTable* live_code_table,
                         CodeRegionTable* dead_code_table,
                         CodeRegionTable* tag_code_table)
      : SampleVisitor(isolate),
        live_code_table_(live_code_table),
        dead_code_table_(dead_code_table),
        tag_code_table_(tag_code_table),
        isolate_(isolate),
        vm_isolate_(Dart::vm_isolate()) {
    ASSERT(live_code_table_ != NULL);
    ASSERT(dead_code_table_ != NULL);
    ASSERT(tag_code_table_ != NULL);
    frames_ = 0;
    min_time_ = kMaxInt64;
    max_time_ = 0;
    ASSERT(isolate_ != NULL);
    ASSERT(vm_isolate_ != NULL);
  }

  void VisitSample(Sample* sample) {
    int64_t timestamp = sample->timestamp();
    if (timestamp > max_time_) {
      max_time_ = timestamp;
    }
    if (timestamp < min_time_) {
      min_time_ = timestamp;
    }
    // Make sure VM tag is created.
    CreateTag(sample->vm_tag());
    // Exclusive tick for bottom frame.
    Tick(sample->At(0), true, timestamp);
    // Inclusive tick for all frames.
    for (intptr_t i = 0; i < FLAG_profile_depth; i++) {
      if (sample->At(i) == 0) {
        break;
      }
      frames_++;
      Tick(sample->At(i), false, timestamp);
    }
  }

  intptr_t frames() const { return frames_; }

  intptr_t  TimeDeltaMicros() const {
    return static_cast<intptr_t>(max_time_ - min_time_);
  }
  int64_t  max_time() const { return max_time_; }

 private:
  void CreateTag(uword tag) {
    intptr_t index = tag_code_table_->FindIndex(tag);
    if (index >= 0) {
      // Already created.
      return;
    }
    CodeRegion* region = new CodeRegion(CodeRegion::kTagCode,
                                        tag,
                                        tag + 1,
                                        0);
    index = tag_code_table_->InsertCodeRegion(region);
    ASSERT(index >= 0);
    region->set_creation_serial(visited());
  }

  void Tick(uword pc, bool exclusive, int64_t timestamp) {
    CodeRegionTable::TickResult r;
    intptr_t serial = exclusive ? -1 : visited();
    r = live_code_table_->Tick(pc, exclusive, serial, timestamp);
    if (r == CodeRegionTable::kTicked) {
      // Live code found and ticked.
      return;
    }
    if (r == CodeRegionTable::kNewerCode) {
      // Code has been overwritten by newer code.
      // Update shadow table of dead code regions.
      r = dead_code_table_->Tick(pc, exclusive, serial, timestamp);
      ASSERT(r != CodeRegionTable::kNewerCode);
      if (r == CodeRegionTable::kTicked) {
        // Dead code found and ticked.
        return;
      }
      ASSERT(r == CodeRegionTable::kNotFound);
      CreateAndTickDeadCodeRegion(pc, exclusive, serial);
      return;
    }
    // Create new live CodeRegion.
    ASSERT(r == CodeRegionTable::kNotFound);
    CodeRegion* region = CreateCodeRegion(pc);
    region->set_creation_serial(visited());
    intptr_t index = live_code_table_->InsertCodeRegion(region);
    ASSERT(index >= 0);
    region = live_code_table_->At(index);
    if (region->compile_timestamp() <= timestamp) {
      region->Tick(pc, exclusive, serial);
      return;
    }
    // We have created a new code region but it's for a CodeRegion
    // compiled after the sample.
    ASSERT(region->kind() == CodeRegion::kDartCode);
    CreateAndTickDeadCodeRegion(pc, exclusive, serial);
  }

  void CreateAndTickDeadCodeRegion(uword pc, bool exclusive, intptr_t serial) {
    // Need to create dead code.
    CodeRegion* region = new CodeRegion(CodeRegion::kReusedCode,
                                        pc,
                                        pc + 1,
                                        0);
    intptr_t index = dead_code_table_->InsertCodeRegion(region);
    region->set_creation_serial(visited());
    ASSERT(index >= 0);
    dead_code_table_->At(index)->Tick(pc, exclusive, serial);
  }

  CodeRegion* CreateCodeRegion(uword pc) {
    const intptr_t kDartCodeAlignment = OS::PreferredCodeAlignment();
    const intptr_t kDartCodeAlignmentMask = ~(kDartCodeAlignment - 1);
    Code& code = Code::Handle(isolate_);
    // Check current isolate for pc.
    if (isolate_->heap()->CodeContains(pc)) {
      code ^= Code::LookupCode(pc);
      if (!code.IsNull()) {
        return new CodeRegion(CodeRegion::kDartCode, code.EntryPoint(),
                              code.EntryPoint() + code.Size(),
                              code.compile_timestamp());
      }
      return new CodeRegion(CodeRegion::kCollectedCode, pc,
                            (pc & kDartCodeAlignmentMask) + kDartCodeAlignment,
                            0);
    }
    // Check VM isolate for pc.
    if (vm_isolate_->heap()->CodeContains(pc)) {
      code ^= Code::LookupCodeInVmIsolate(pc);
      if (!code.IsNull()) {
        return new CodeRegion(CodeRegion::kDartCode, code.EntryPoint(),
                              code.EntryPoint() + code.Size(),
                              code.compile_timestamp());
      }
      return new CodeRegion(CodeRegion::kCollectedCode, pc,
                            (pc & kDartCodeAlignmentMask) + kDartCodeAlignment,
                            0);
    }
    // Check NativeSymbolResolver for pc.
    uintptr_t native_start = 0;
    char* native_name = NativeSymbolResolver::LookupSymbolName(pc,
                                                               &native_start);
    if (native_name == NULL) {
      // No native name found.
      return new CodeRegion(CodeRegion::kNativeCode, pc, pc + 1, 0);
    }
    ASSERT(pc >= native_start);
    CodeRegion* code_region =
        new CodeRegion(CodeRegion::kNativeCode, native_start, pc + 1, 0);
    code_region->SetName(native_name);
    free(native_name);
    return code_region;
  }

  intptr_t frames_;
  int64_t min_time_;
  int64_t max_time_;
  CodeRegionTable* live_code_table_;
  CodeRegionTable* dead_code_table_;
  CodeRegionTable* tag_code_table_;
  Isolate* isolate_;
  Isolate* vm_isolate_;
};


class CodeRegionExclusiveTrieBuilder : public SampleVisitor {
 public:
  CodeRegionExclusiveTrieBuilder(Isolate* isolate,
                                 CodeRegionTable* live_code_table,
                                 CodeRegionTable* dead_code_table,
                                 CodeRegionTable* tag_code_table)
      : SampleVisitor(isolate),
        live_code_table_(live_code_table),
        dead_code_table_(dead_code_table),
        tag_code_table_(tag_code_table) {
    ASSERT(live_code_table_ != NULL);
    ASSERT(dead_code_table_ != NULL);
    ASSERT(tag_code_table_ != NULL);
    dead_code_table_offset_ = live_code_table_->Length();
    tag_code_table_offset_ = dead_code_table_offset_ +
                             dead_code_table_->Length();
    intptr_t root_index = tag_code_table_->FindIndex(0);
    // Verify that the "0" tag does not exist.
    ASSERT(root_index < 0);
    // Insert the dummy tag CodeRegion that is used for the Trie root.
    CodeRegion* region = new CodeRegion(CodeRegion::kTagCode, 0, 1, 0);
    root_index = tag_code_table_->InsertCodeRegion(region);
    ASSERT(root_index >= 0);
    region->set_creation_serial(0);
    root_ = new CodeRegionTrieNode(tag_code_table_offset_ + root_index);
    // Use tags by default.
    set_use_tags(true);
  }

  void VisitSample(Sample* sample) {
    // Give the root a tick.
    root_->Tick();
    CodeRegionTrieNode* current = root_;
    if (use_tags()) {
      intptr_t tag_index = FindTagIndex(sample->vm_tag());
      current = current->GetChild(tag_index);
      // Give the tag a tick.
      current->Tick();
    }
    // Walk the sampled PCs.
    for (intptr_t i = 0; i < FLAG_profile_depth; i++) {
      if (sample->At(i) == 0) {
        break;
      }
      intptr_t index = FindFinalIndex(sample->At(i), sample->timestamp());
      current = current->GetChild(index);
      current->Tick();
    }
  }

  CodeRegionTrieNode* root() const {
    return root_;
  }

  bool use_tags() const {
    return use_tags_;
  }

  void set_use_tags(bool use_tags) {
    use_tags_ = use_tags;
  }

 private:
  intptr_t FindTagIndex(uword tag) const {
    intptr_t index = tag_code_table_->FindIndex(tag);
    ASSERT(index >= 0);
    ASSERT((tag_code_table_->At(index))->contains(tag));
    return tag_code_table_offset_ + index;
  }

  intptr_t FindFinalIndex(uword pc, int64_t timestamp) const {
    intptr_t index = live_code_table_->FindIndex(pc);
    ASSERT(index >= 0);
    CodeRegion* region = live_code_table_->At(index);
    ASSERT(region->contains(pc));
    if (region->compile_timestamp() > timestamp) {
      // Overwritten code, find in dead code table.
      index = dead_code_table_->FindIndex(pc);
      ASSERT(index >= 0);
      region = dead_code_table_->At(index);
      ASSERT(region->contains(pc));
      ASSERT(region->compile_timestamp() <= timestamp);
      return index + dead_code_table_offset_;
    }
    ASSERT(region->compile_timestamp() <= timestamp);
    return index;
  }

  bool use_tags_;
  CodeRegionTrieNode* root_;
  CodeRegionTable* live_code_table_;
  CodeRegionTable* dead_code_table_;
  CodeRegionTable* tag_code_table_;
  intptr_t dead_code_table_offset_;
  intptr_t tag_code_table_offset_;
};


class CodeRegionTableCallersBuilder {
 public:
  CodeRegionTableCallersBuilder(CodeRegionTrieNode* exclusive_root,
                                CodeRegionTable* live_code_table,
                                CodeRegionTable* dead_code_table,
                                CodeRegionTable* tag_code_table)
      : exclusive_root_(exclusive_root),
        live_code_table_(live_code_table),
        dead_code_table_(dead_code_table),
        tag_code_table_(tag_code_table) {
    ASSERT(exclusive_root_ != NULL);
    ASSERT(live_code_table_ != NULL);
    ASSERT(dead_code_table_ != NULL);
    ASSERT(tag_code_table_ != NULL);
    dead_code_table_offset_ = live_code_table_->Length();
    tag_code_table_offset_ = dead_code_table_offset_ +
                             dead_code_table_->Length();
  }

  void Build() {
    ProcessNode(exclusive_root_);
  }

 private:
  void ProcessNode(CodeRegionTrieNode* parent) {
    const ZoneGrowableArray<CodeRegionTrieNode*>& children = parent->children();
    intptr_t parent_index = parent->code_region_index();
    ASSERT(parent_index >= 0);
    CodeRegion* parent_region = At(parent_index);
    ASSERT(parent_region != NULL);
    for (intptr_t i = 0; i < children.length(); i++) {
      CodeRegionTrieNode* node = children[i];
      ProcessNode(node);
      intptr_t index = node->code_region_index();
      ASSERT(index >= 0);
      CodeRegion* region = At(index);
      ASSERT(region != NULL);
      region->AddCallee(parent_index, node->count());
      parent_region->AddCaller(index, node->count());
    }
  }

  CodeRegion* At(intptr_t final_index) {
    ASSERT(final_index >= 0);
    if (final_index < dead_code_table_offset_) {
      return live_code_table_->At(final_index);
    } else if (final_index < tag_code_table_offset_) {
      return dead_code_table_->At(final_index - dead_code_table_offset_);
    } else {
      return tag_code_table_->At(final_index - tag_code_table_offset_);
    }
  }

  CodeRegionTrieNode* exclusive_root_;
  CodeRegionTable* live_code_table_;
  CodeRegionTable* dead_code_table_;
  CodeRegionTable* tag_code_table_;
  intptr_t dead_code_table_offset_;
  intptr_t tag_code_table_offset_;
};


void Profiler::PrintToJSONStream(Isolate* isolate, JSONStream* stream,
                                 bool full, bool use_tags) {
  ASSERT(isolate == Isolate::Current());
  // Disable profile interrupts while processing the buffer.
  EndExecution(isolate);
  MutexLocker profiler_data_lock(isolate->profiler_data_mutex());
  IsolateProfilerData* profiler_data = isolate->profiler_data();
  if (profiler_data == NULL) {
    JSONObject error(stream);
    error.AddProperty("type", "Error");
    error.AddProperty("text", "Isolate does not have profiling enabled.");
    return;
  }
  SampleBuffer* sample_buffer = profiler_data->sample_buffer();
  ASSERT(sample_buffer != NULL);
  {
    StackZone zone(isolate);
    {
      // Live code holds Dart, Native, and Collected CodeRegions.
      CodeRegionTable live_code_table;
      // Dead code holds Overwritten CodeRegions.
      CodeRegionTable dead_code_table;
      // Tag code holds Tag CodeRegions.
      CodeRegionTable tag_code_table;
      CodeRegionTableBuilder builder(isolate,
                                     &live_code_table,
                                     &dead_code_table,
                                     &tag_code_table);
      {
        // Build CodeRegion tables.
        ScopeStopwatch sw("CodeRegionTableBuilder");
        sample_buffer->VisitSamples(&builder);
      }
      intptr_t samples = builder.visited();
      intptr_t frames = builder.frames();
      if (FLAG_trace_profiled_isolates) {
        intptr_t total_live_code_objects = live_code_table.Length();
        intptr_t total_dead_code_objects = dead_code_table.Length();
        intptr_t total_tag_code_objects = tag_code_table.Length();
        OS::Print("Processed %" Pd " frames\n", frames);
        OS::Print("CodeTables: live=%" Pd " dead=%" Pd " tag=%" Pd "\n",
                  total_live_code_objects,
                  total_dead_code_objects,
                  total_tag_code_objects);
      }
#if defined(DEBUG)
      live_code_table.Verify();
      dead_code_table.Verify();
      tag_code_table.Verify();
      if (FLAG_trace_profiled_isolates) {
        OS::Print("CodeRegionTables verified to be ordered and not overlap.\n");
      }
#endif
      CodeRegionExclusiveTrieBuilder build_trie(isolate,
                                                &live_code_table,
                                                &dead_code_table,
                                                &tag_code_table);
      build_trie.set_use_tags(use_tags);
      {
        // Build CodeRegion trie.
        ScopeStopwatch sw("CodeRegionExclusiveTrieBuilder");
        sample_buffer->VisitSamples(&build_trie);
        build_trie.root()->SortByCount();
      }
      CodeRegionTableCallersBuilder build_callers(build_trie.root(),
                                                  &live_code_table,
                                                  &dead_code_table,
                                                  &tag_code_table);
      {
        // Build CodeRegion callers.
        ScopeStopwatch sw("CodeRegionTableCallersBuilder");
        build_callers.Build();
      }
      {
        ScopeStopwatch sw("CodeTableStream");
        // Serialize to JSON.
        JSONObject obj(stream);
        obj.AddProperty("type", "Profile");
        obj.AddProperty("id", "profile");
        obj.AddProperty("samples", samples);
        obj.AddProperty("depth", static_cast<intptr_t>(FLAG_profile_depth));
        obj.AddProperty("period", static_cast<intptr_t>(FLAG_profile_period));
        obj.AddProperty("time_delta_micros", builder.TimeDeltaMicros());
        {
          JSONArray exclusive_trie(&obj, "exclusive_trie");
          CodeRegionTrieNode* root = build_trie.root();
          ASSERT(root != NULL);
          root->PrintToJSONArray(&exclusive_trie);
        }
        JSONArray codes(&obj, "codes");
        for (intptr_t i = 0; i < live_code_table.Length(); i++) {
          CodeRegion* region = live_code_table.At(i);
          ASSERT(region != NULL);
          region->PrintToJSONArray(isolate, &codes, full);
        }
        for (intptr_t i = 0; i < dead_code_table.Length(); i++) {
          CodeRegion* region = dead_code_table.At(i);
          ASSERT(region != NULL);
          region->PrintToJSONArray(isolate, &codes, full);
        }
        for (intptr_t i = 0; i < tag_code_table.Length(); i++) {
          CodeRegion* region = tag_code_table.At(i);
          ASSERT(region != NULL);
          region->PrintToJSONArray(isolate, &codes, full);
        }
      }
    }
  }
  // Enable profile interrupts.
  BeginExecution(isolate);
}


void Profiler::WriteProfile(Isolate* isolate) {
  if (isolate == NULL) {
    return;
  }
  if (!FLAG_profile) {
    return;
  }
  ASSERT(initialized_);
  if (FLAG_profile_dir == NULL) {
    return;
  }
  Dart_FileOpenCallback file_open = Isolate::file_open_callback();
  Dart_FileCloseCallback file_close = Isolate::file_close_callback();
  Dart_FileWriteCallback file_write = Isolate::file_write_callback();
  if ((file_open == NULL) || (file_close == NULL) || (file_write == NULL)) {
    // Embedder has not provided necessary callbacks.
    return;
  }
  // We will be looking up code objects within the isolate.
  ASSERT(Isolate::Current() == isolate);
  JSONStream stream(10 * MB);
  intptr_t pid = OS::ProcessId();
  PrintToJSONStream(isolate, &stream, true, false);
  const char* format = "%s/dart-profile-%" Pd "-%" Pd ".json";
  intptr_t len = OS::SNPrint(NULL, 0, format,
                             FLAG_profile_dir, pid, isolate->main_port());
  char* filename = Isolate::Current()->current_zone()->Alloc<char>(len + 1);
  OS::SNPrint(filename, len + 1, format,
              FLAG_profile_dir, pid, isolate->main_port());
  void* f = file_open(filename, true);
  if (f == NULL) {
    // Cannot write.
    return;
  }
  TextBuffer* buffer = stream.buffer();
  ASSERT(buffer != NULL);
  file_write(buffer->buf(), buffer->length(), f);
  file_close(f);
}


IsolateProfilerData::IsolateProfilerData(SampleBuffer* sample_buffer,
                                         bool own_sample_buffer) {
  ASSERT(sample_buffer != NULL);
  sample_buffer_ = sample_buffer;
  own_sample_buffer_ = own_sample_buffer;
}


IsolateProfilerData::~IsolateProfilerData() {
  if (own_sample_buffer_) {
    delete sample_buffer_;
    sample_buffer_ = NULL;
    own_sample_buffer_ = false;
  }
}


Sample* SampleBuffer::ReserveSample() {
  ASSERT(samples_ != NULL);
  uintptr_t cursor = AtomicOperations::FetchAndIncrement(&cursor_);
  // Map back into sample buffer range.
  cursor = cursor % capacity_;
  return At(cursor);
}

// Notes on stack frame walking:
//
// The sampling profiler will collect up to Sample::kNumStackFrames stack frames
// The stack frame walking code uses the frame pointer to traverse the stack.
// If the VM is compiled without frame pointers (which is the default on
// recent GCC versions with optimizing enabled) the stack walking code may
// fail (sometimes leading to a crash).
//
class ProfilerSampleStackWalker : public ValueObject {
 public:
  ProfilerSampleStackWalker(Sample* sample,
                            uword stack_lower,
                            uword stack_upper,
                            uword pc,
                            uword fp,
                            uword sp)
      : sample_(sample),
        stack_upper_(stack_upper),
        original_pc_(pc),
        original_fp_(fp),
        original_sp_(sp),
        lower_bound_(stack_lower) {
    ASSERT(sample_ != NULL);
  }

  int walk(Heap* heap, uword vm_tag) {
    const intptr_t kMaxStep = 0x1000;  // 4K.
    const bool kWalkStack = true;  // Walk the stack.
    // Always store the exclusive PC.
    sample_->SetAt(0, original_pc_);
    // Always store the vm tag.
    sample_->set_vm_tag(vm_tag);
    if (!kWalkStack) {
      // Not walking the stack, only took exclusive sample.
      return 1;
    }
    uword* pc = reinterpret_cast<uword*>(original_pc_);
    uword* fp = reinterpret_cast<uword*>(original_fp_);
    uword* previous_fp = fp;
    if (original_sp_ > original_fp_) {
      // Stack pointer should not be above frame pointer.
      return 1;
    }
    intptr_t gap = original_fp_ - original_sp_;
    if (gap >= kMaxStep) {
      // Gap between frame pointer and stack pointer is
      // too large.
      return 1;
    }
    if (original_sp_ < lower_bound_) {
      // The stack pointer gives us a better lower bound than
      // the isolates stack limit.
      lower_bound_ = original_sp_;
    }
    int i = 0;
    for (; i < FLAG_profile_depth; i++) {
      if (FLAG_profile_verify_stack_walk) {
        VerifyCodeAddress(heap, i, reinterpret_cast<uword>(pc));
      }
      sample_->SetAt(i, reinterpret_cast<uword>(pc));
      if (!ValidFramePointer(fp)) {
        return i + 1;
      }
      pc = CallerPC(fp);
      previous_fp = fp;
      fp = CallerFP(fp);
      intptr_t step = fp - previous_fp;
      if ((step >= kMaxStep) || (fp <= previous_fp) || !ValidFramePointer(fp)) {
        // Frame pointer step is too large.
        // Frame pointer did not move to a higher address.
        // Frame pointer is outside of isolate stack bounds.
        return i + 1;
      }
      // Move the lower bound up.
      lower_bound_ = reinterpret_cast<uword>(fp);
    }
    return i;
  }

 private:
  void VerifyCodeAddress(Heap* heap, int i, uword pc) {
    if (heap != NULL) {
      if (heap->Contains(pc) && !heap->CodeContains(pc)) {
        for (int j = 0; j < i; j++) {
          OS::Print("%d %" Px "\n", j, sample_->At(j));
        }
        OS::Print("%d %" Px " <--\n", i, pc);
        OS::Print("---ASSERT-FAILED---\n");
        OS::Print("%" Px " %" Px "\n", original_pc_, original_fp_);
        UNREACHABLE();
      }
    }
  }

  uword* CallerPC(uword* fp) const {
    ASSERT(fp != NULL);
    return reinterpret_cast<uword*>(*(fp + kSavedCallerPcSlotFromFp));
  }

  uword* CallerFP(uword* fp) const {
    ASSERT(fp != NULL);
    return reinterpret_cast<uword*>(*(fp + kSavedCallerFpSlotFromFp));
  }

  bool ValidFramePointer(uword* fp) const {
    if (fp == NULL) {
      return false;
    }
    uword cursor = reinterpret_cast<uword>(fp);
    cursor += sizeof(fp);
    bool r = cursor >= lower_bound_ && cursor < stack_upper_;
    return r;
  }


  Sample* sample_;
  const uword stack_upper_;
  const uword original_pc_;
  const uword original_fp_;
  const uword original_sp_;
  uword lower_bound_;
};


void Profiler::RecordSampleInterruptCallback(
    const InterruptedThreadState& state,
    void* data) {
  Isolate* isolate = reinterpret_cast<Isolate*>(data);
  if (isolate == NULL) {
    return;
  }
  VMTagCounters* counters = isolate->vm_tag_counters();
  ASSERT(counters != NULL);
  counters->Increment(isolate->vm_tag());
  IsolateProfilerData* profiler_data = isolate->profiler_data();
  if (profiler_data == NULL) {
    return;
  }
  SampleBuffer* sample_buffer = profiler_data->sample_buffer();
  if (sample_buffer == NULL) {
    return;
  }
  Sample* sample = sample_buffer->ReserveSample();
  sample->Init(isolate, OS::GetCurrentTimeMicros(), state.tid);
  uword stack_lower = 0;
  uword stack_upper = 0;
  isolate->GetStackBounds(&stack_lower, &stack_upper);
  if ((stack_lower == 0) || (stack_upper == 0)) {
    stack_lower = 0;
    stack_upper = 0;
  }
  ProfilerSampleStackWalker stackWalker(sample, stack_lower, stack_upper,
                                        state.pc, state.fp, state.sp);
  stackWalker.walk(isolate->heap(), isolate->vm_tag());
}

}  // namespace dart
