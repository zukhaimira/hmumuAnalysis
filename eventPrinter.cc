#include <TSystem.h>
#include <TROOT.h>
#include <TClonesArray.h>
#include <TParticle.h>
#include <iostream>
#include <TFile.h>
#include <TTree.h>
#include <TChain.h>
#include <TH2F.h>
#include <TH1F.h>
#include <TProfile.h>
#include <TCanvas.h>
#include <TStyle.h>
#include <TLegend.h>
#include <TF1.h>
#include <TRandom3.h>

#include "TMVA/Tools.h"
#include "TMVA/Reader.h"
#include "TMVA/MethodCuts.h"

#include "DataFormats.h"
#include "helpers.h"
#include "mva.h"
#include "LumiReweightingStandAlone.h"

#include "boost/program_options.hpp"
#include "boost/regex.hpp"
#include "boost/algorithm/string.hpp"
#include <boost/lexical_cast.hpp>

#include <limits.h>

#define JETPUID
#define PUREWEIGHT

using namespace std;
using namespace boost;

int main(int argc, char *argv[])
{
  /////////////////////////////////////////////
  //////////// Configuration Options //////////
  /////////////////////////////////////////////

  const char* optionIntro = "H->MuMu Analyzer\n\nUsage: ./analyzer [--help] [--train] [--maxEvents N] <outputFileName.root> <inputFileName.root> [<inputFileName2.root>...]\n\nAllowed Options";
  program_options::options_description optionDesc(optionIntro);
  optionDesc.add_options()
      ("help,h", "Produce Help Message")
      ("filenames",program_options::value<vector<string> >(), "Input & Output File Names, put output name first followed by all input file names")
      ("eventStr,e",program_options::value<vector<string> >(), "Run:Event Strings")
  ;
  
  program_options::positional_options_description optionPos;
  optionPos.add("filenames",-1);
  
  program_options::variables_map optionMap;
  program_options::store(program_options::command_line_parser(argc, argv).options(optionDesc).positional(optionPos).run(), optionMap);
  program_options::notify(optionMap);    
  
  if (optionMap.count("help")) 
  {
      cout << optionDesc << "\n";
      return 1;
  }

  vector<std::string> filenames;
  vector<string>::const_iterator filename;
  std::string outputFileName;
  if (optionMap.count("filenames")>0)
  {
     filenames = optionMap["filenames"].as<vector<string> >();
     if(filenames.size()<2)
     {
       cout << "Error: Need both input file and output file names, exiting." << endl;
       return 1;
     }
  }
  else
  {
     cout << "Error: Input file name  and ouput file name arguments required, exiting." << endl;
     return 1;
  }

  vector<std::string> eventStrs;
  vector<string>::const_iterator eventStr;
  if (optionMap.count("eventStr")>0)
  {
     eventStrs = optionMap["eventStr"].as<vector<string> >();
     if(eventStrs.size()<1)
     {
       cout << "Error: No Wanted events found, use -e <run>:<event> option." << endl;
       return 1;
     }
  }
  else
  {
     cout << "Error: No Wanted events found, use -e <run>:<event> option." << endl;
     return 1;
  }

  unsigned nEventsDesired = eventStrs.size();
  vector<unsigned> runs(eventStrs.size());
  vector<unsigned> events(eventStrs.size());
  unsigned tmpMissed = 0;
  for (unsigned i=0;i<nEventsDesired;i++)
  {
    std::vector<std::string> splitStrings;
    boost::split(splitStrings, eventStrs[i], boost::is_any_of(":"));
    if(splitStrings.size() != 2)
    {
      tmpMissed++;
      cerr << "Error: Didn't Correctly Parse Event String: "<< eventStrs[i] << endl;
    }
    runs[i] = boost::lexical_cast<unsigned>(splitStrings[0]);
    events[i] = boost::lexical_cast<unsigned>(splitStrings[1]);
  }
  nEventsDesired -= tmpMissed;

  /////////////////////////////
  //////////// Setup //////////
  /////////////////////////////

  ////////////
  
  TChain * tree = new TChain("tree");

  cout << "Input File Names: \n"; 
  for(filename = filenames.begin();filename != filenames.end();filename++)
  {
    cout<<"  "<< *filename << endl;
    tree->AddFile(filename->c_str());
  }

  //////////////////////////
  // Tree Branches

  _MuonInfo reco1, reco2;

  tree->SetBranchAddress("reco1", &reco1);
  tree->SetBranchAddress("reco2", &reco2);

  float recoCandMass, recoCandPt, recoCandY, recoCandPhi;

  tree->SetBranchAddress("recoCandMass", &recoCandMass);
  tree->SetBranchAddress("recoCandPt"  , &recoCandPt );
  tree->SetBranchAddress("recoCandY"  , &recoCandY );
  tree->SetBranchAddress("recoCandPhi"  , &recoCandPhi );

  float trueMass=-99999.0;
  if(tree->GetBranchStatus("trueMass"))
    tree->SetBranchAddress("trueMass", &trueMass);

  _PFJetInfo jets;
  tree->SetBranchAddress("pfJets",&jets);

#ifdef JETPUID
  float puJetFullDisc[10];
  float puJetSimpleDisc[10];
  float puJetCutDisc[10];

  tree->SetBranchAddress("puJetFullDisc",&puJetFullDisc);
  tree->SetBranchAddress("puJetSimpleDisc",&puJetSimpleDisc);
  tree->SetBranchAddress("puJetCutDisc",&puJetCutDisc);

  int puJetFullId[10];
  int puJetSimpleId[10];
  int puJetCutId[10];

  tree->SetBranchAddress("puJetFullId",&puJetFullId);
  tree->SetBranchAddress("puJetSimpleId",&puJetSimpleId);
  tree->SetBranchAddress("puJetCutId",&puJetCutId);
#endif

  int nPU=0;
#ifdef PUREWEIGHT
  if(tree->GetBranchStatus("nPU"))
    tree->SetBranchAddress("nPU",&nPU);
#endif
  _VertexInfo vertexInfo;
  tree->SetBranchAddress("vertexInfo",&vertexInfo);
  _EventInfo eventInfo;
  //TBranch* eventInfoBranch = tree->GetBranch("eventInfo");
  //eventInfoBranch->SetAddress(&eventInfo);
  tree->SetBranchAddress("eventInfo",&eventInfo);
  _MetInfo met;
  tree->SetBranchAddress("met",&met);

cout.precision(5);

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

  unsigned nEvents = tree->GetEntries();
  cout << "nEvents: " << nEvents << endl;

  unsigned reportEach=1000;
  if (nEvents/1000>reportEach)
    reportEach = nEvents/1000;

  unsigned foundCounter = 0;
  
  for(unsigned i=0; i<nEvents;i++)
  {
    tree->GetEvent(i);
    //eventInfoBranch->GetEvent(i);
    bool wantedEvent = false;
    for(unsigned j=0;j<nEventsDesired;j++)
    {
      if (eventInfo.run==runs[j] && eventInfo.event==events[j])
      {
        wantedEvent=true;
        break;
      }
    }
    if(!wantedEvent)
      continue;
    //tree->GetEvent(i);

    _MuonInfo muon1=reco1;
    _MuonInfo muon2=reco2;
    if(reco1.pt<reco2.pt)
    {
        muon1 = reco2;
        muon2 = reco1;
    }

    unsigned nJets = 0;
    for(unsigned j=0;j<10;j++)
    {
      if (jets.pt[j]<30.0)
      {
        break;
      }
      nJets++;
    }

    cout << "Run:Event (Lumi) = "<<eventInfo.run<<":"<<eventInfo.event<<" ("<<eventInfo.lumi<<")\n";
    cout << "  Dimuon M = "<<recoCandMass<<" \t Pt = "<<recoCandPt << " \t Y = "<<recoCandY <<"\n";
    cout << "   Lead Mu Pt = "<< muon1.pt << " \t Eta = "<<muon1.eta << " \t Phi = "<<muon1.phi << " \t Q = "<<muon1.charge <<"\n";
    cout << "   2nd Mu Pt  = "<< muon2.pt << " \t Eta = "<<muon2.eta << " \t Phi = "<<muon2.phi << " \t Q = "<<muon2.charge <<"\n";
    cout << "     Nvtx = "<< vertexInfo.nVertices <<" \t nJets = "<<nJets<<"\n";
    cout << endl;

    foundCounter++;
    if(foundCounter >= nEventsDesired)
        break;
  }// end event loop

//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

  cout << "Found "<<foundCounter<<"/"<<nEventsDesired << endl;
  cout << "done." << endl << endl;
  return 0;
}
