#include "UtilESM.h"

#include <boost/foreach.hpp>
#include <boost/algorithm/string.hpp>
#include <iostream>
#include <sstream>
#include <queue>
#include <algorithm> 

#include "moses/FF/Diffs.h"

namespace Moses
{

typedef std::vector<std::string> Tokens;

std::vector<std::string> CreateSinglePattern(const Tokens &s1, const Tokens &s2) {
  std::vector<std::string> pattern;
  
  if(s1.empty()) {
    BOOST_FOREACH(std::string w, s2) {
     std::stringstream out;
     out << "+_" << w;
     pattern.push_back(out.str());
    }
  }
  else if(s2.empty()) {
    BOOST_FOREACH(std::string w, s2) {
     std::stringstream out;
     out << "-_" << w;
     pattern.push_back(out.str());
    }
  }
  else {
    for(size_t i = 0; i < std::max(s1.size(), s2.size()); i++) {
      std::stringstream out;
      if(i < s1.size() && i < s2.size()) {
        if(s1[i] == s2[i])
          out << "=_" << s1[i];
        else
          out << "~_" << s1[i] << "_" << s2[i];
      }
      else if(i < s1.size()) {
        out << "-_" << s1[i];   
      }
      else if(i < s2.size()) {
        out << "+_" << s2[i];   
      }
      pattern.push_back(out.str());
    }
  }
  
  return pattern;
}

void calculateEdits(
    std::vector<std::string>& patternList,
    const std::vector<std::string>& s1,
    const std::vector<std::string>& s2) {
      
  Diffs diffs = CreateDiff(s1, s2);
  size_t i = 0, j = 0;
  char lastType = 'm';
  Tokens source, target;
  BOOST_FOREACH(Diff type, diffs) {
    if(type == 'm') {
      if(lastType != 'm') {
        std::vector<std::string> patterns = CreateSinglePattern(source, target);
        patternList.insert(patternList.end(), patterns.begin(), patterns.end());
      }
      source.clear();
      target.clear();
      
      if(s1[i] != s2[j]) {
        source.push_back(s1[i]);
        target.push_back(s2[j]);
      }
      else {
        source.push_back(s1[i]);
        target.push_back(s2[j]);
        std::vector<std::string> patterns = CreateSinglePattern(source, target);
        patternList.insert(patternList.end(), patterns.begin(), patterns.end());
        source.clear();
        target.clear();
      }
      
      i++;
      j++;
    }
    else if(type == 'd') {
      source.push_back(s1[i]);
      i++;
    }
    else if(type == 'i') {
      target.push_back(s2[j]);
      j++;
    }
    lastType = type;
  }
  if(lastType != 'm') {    
    if(!source.empty() || !target.empty()) {
      std::vector<std::string> patterns = CreateSinglePattern(source, target);
      patternList.insert(patternList.end(), patterns.begin(), patterns.end());
    }
  }  
}

typedef std::vector<size_t> Cept;
typedef std::pair<Cept, Cept> CeptPair;

struct CeptSorter {
  bool operator()(const CeptPair& c1, const CeptPair& c2) const {
    if(!c1.second.empty() && !c2.second.empty())
      return c1.second < c2.second;

    if(!c1.first.empty() && !c2.first.empty())
      return c1.first < c2.first;

    if(!c1.second.empty() && c2.second.empty())
      return c1.first < c2.second;

    if(c1.second.empty() && !c2.second.empty())
      return c1.second < c2.first;
    
    return false;
  }
};

typedef std::vector<CeptPair> CeptSequence;

void calculateCepts(CeptSequence &cepts, const Moses::AlignmentInfo& alignment, size_t slen, size_t tlen) {
  
  std::vector<std::vector<size_t> > sourceAligned(slen);
  std::vector<std::vector<size_t> > targetAligned(tlen);
  
  AlignmentInfo::const_iterator iter;
  for (iter = alignment.begin(); iter != alignment.end(); ++iter) {
    sourceAligned[iter->first].push_back(iter->second);
    targetAligned[iter->second].push_back(iter->first);
  }

  std::vector<bool> sourceVisited(slen, false);
  std::vector<bool> targetVisited(tlen, false);
  
  for(size_t i = 0; i < tlen; ++i) {
    if(!targetVisited[i]) {
      typedef std::pair<size_t, bool> QueueItem;
      std::queue<QueueItem> myQueue;
      myQueue.push(std::make_pair(i, true));
      
      CeptPair cp;
      
      while(!myQueue.empty()) {
        QueueItem item = myQueue.front();
        myQueue.pop();
              
        if(item.second) {
          if(!targetVisited[item.first]) {
            targetVisited[item.first] = true;
            cp.second.push_back(item.first);
            BOOST_FOREACH(size_t j, targetAligned[item.first])
              myQueue.push(std::make_pair(j, false));
          }
        }
        else {
          if(!sourceVisited[item.first]) {
            sourceVisited[item.first] = true;
            cp.first.push_back(item.first);
            BOOST_FOREACH(size_t j, sourceAligned[item.first])
              myQueue.push(std::make_pair(j, true));
          }
        }
      }
      std::sort(cp.first.begin(), cp.first.end());
      std::sort(cp.second.begin(), cp.second.end());
      cepts.push_back(cp);
    }
  }
  for(size_t i = 0; i < slen; ++i) {
    if(!sourceVisited[i]) {
      CeptPair cp;
      cp.first.push_back(i);
      cepts.push_back(cp);
    }
  }
  
  CeptSorter ceptSorter;
  std::sort(cepts.begin(), cepts.end(), ceptSorter);
}

void calculateEdits(
    std::vector<std::string>& edits,
    const std::vector<std::string>& source,
    const std::vector<std::string>& target,
    const Moses::AlignmentInfo& alignment) {
  
  CeptSequence ceptSequence;
  calculateCepts(ceptSequence, alignment, source.size(), target.size());
  
  edits.reserve(ceptSequence.size());
  
  BOOST_FOREACH(CeptPair cp, ceptSequence) {
    Cept& sourceCept = cp.first;
    Cept& targetCept = cp.second;
    
    std::stringstream sourceStream;
    if(!sourceCept.empty()) {
      Cept::iterator iter = sourceCept.begin();
      sourceStream << source[*iter++];    
      while(iter != sourceCept.end())
        sourceStream << "^" << source[*iter++];
    }
    
    std::stringstream targetStream;
    if(!targetCept.empty()) {
      Cept::iterator iter = targetCept.begin();
      targetStream << target[*iter++];    
      while(iter != targetCept.end())
        targetStream << "^" << target[*iter++];
    }
    
    std::string sourceStr = sourceStream.str();
    std::string targetStr = targetStream.str();
    if(!sourceStr.empty() && !targetStr.empty()) {
      std::stringstream op;
      if(sourceStr == targetStr)
        op << "=_" << sourceStr;
      else
        op << "~_" << sourceStr << "_" << targetStr;
      edits.push_back(op.str());
    }
    else if(!sourceStr.empty() && targetStr.empty()) {
      std::stringstream op;
      op << "-_" << sourceStr;
      edits.push_back(op.str());
    }
    else if(sourceStr.empty() && !targetStr.empty()) {
      std::stringstream op;
      op << "+_" << targetStr;
      edits.push_back(op.str());
    }
  }
}

}