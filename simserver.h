#include "api/Simulator.capnp.h"
#include <kj/debug.h>
#include <kj/filesystem.h>
#include <kj/exception.h>
#include <kj/thread.h>
#include <capnp/ez-rpc.h>
#include <capnp/message.h>
#include <iostream>
#include <Xyce_config.h>
#include <N_CIR_GenCouplingSimulator.h>
#include <N_ERH_Message.h>

using Xyce::IO::OutputType::OutputType;

void report_handler(const char *message, unsigned type)
{
    std::cout << message;
}

struct XyceVectors {
    std::string name;
    std::vector<std::string> fieldnames;
    std::vector<std::vector<double>> real_data;
    std::vector<std::vector<std::complex<double>>> complex_data;
    kj::Maybe<unsigned int> scale;
};

class OutputHandler final : public Xyce::IO::ExternalOutputInterface
{
public:
    OutputHandler(std::string name, Xyce::IO::OutputType::OutputType type, std::vector<std::string> outputs)
        : requested_fieldnames(outputs), type(type), name(name), running(true), selected(false) {}

    std::string getName()
    {
        return name;
    }

    Xyce::IO::OutputType::OutputType getOutputType()
    {
        return type;
    }

    void requestedOutputs(std::vector<std::string> &outputVars)
    {
        outputVars = requested_fieldnames;
    }

    void reportParseStatus(std::vector<bool> & statusVec) {
        for(int i=0; i< statusVec.size(); i++) {
            if (!statusVec[i]) {
                std::cout << "Failed to parse " << requested_fieldnames[i] << std::endl;
            }
        }
    }

    void outputFieldNames(std::vector<std::string> &outputNames)
    {
        *selected.lockExclusive() = true;
        auto vecs = vectors.lockExclusive();
        for (int i = 0; i < numsteps; i++)
        {
            auto num = std::to_string(i);
            XyceVectors step;
            step.name = name + num;
            for (auto field : outputNames)
            {
                step.fieldnames.push_back(field);
            }
            step.real_data.resize(outputNames.size());
            step.complex_data.resize(outputNames.size());
            vecs->push_back(step);
        }
    }

    void newStepOutput(int stepNumber, int maxStep)
    {
        step = stepNumber;
        numsteps = maxStep;
    }

    void outputReal(std::vector<double> &outputData)
    {
        auto vecs = vectors.lockExclusive();
        for (int i = 0; i < outputData.size(); i++)
        {
            (*vecs)[step].real_data[i].push_back(outputData[i]);
        }
    }

    void outputComplex(std::vector<std::complex<double>> &outputData)
    {
        auto vecs = vectors.lockExclusive();
        for (int i = 0; i < outputData.size(); i++)
        {
            (*vecs)[step].complex_data[i].push_back(outputData[i]);
        }
    }

    void finishOutput() {
        *running.lockExclusive() = false;
    };

    std::string name;
    int step = 0;
    int numsteps = 1;
    Xyce::IO::OutputType::OutputType type;
    std::vector<std::string> requested_fieldnames;
    kj::MutexGuarded<std::vector<XyceVectors>> vectors;
    kj::MutexGuarded<bool> running;
    kj::MutexGuarded<bool> selected;
};

class ResultImpl final : public Sim::Result::Server
{
public:
    ResultImpl(std::string path, std::vector<std::string> vecs)
    {
        xyce = kj::heap<Xyce::Circuit::GenCouplingSimulator>();
        Xyce::set_report_handler(report_handler);
        std::vector<const char *> tmpargs;
        tmpargs.push_back("xyce");
        tmpargs.push_back(path.c_str());
        try
        {
            xyce->initializeEarly(tmpargs.size(), const_cast<char **>(tmpargs.data()));
        }
        catch (std::runtime_error &e)
        {
            xyce = nullptr;
            e = std::runtime_error("Fatal error in parsing netlist, check logs");
            throw;
        }
        handlers.push_back(kj::heap<OutputHandler>("tran", Xyce::IO::OutputType::TRAN, vecs));
        handlers.push_back(kj::heap<OutputHandler>("ac", Xyce::IO::OutputType::AC, vecs));
        handlers.push_back(kj::heap<OutputHandler>("op", Xyce::IO::OutputType::DCOP, vecs));
        handlers.push_back(kj::heap<OutputHandler>("dc", Xyce::IO::OutputType::DC, vecs));
        handlers.push_back(kj::heap<OutputHandler>("noise", Xyce::IO::OutputType::NOISE, vecs));
        // ...
        for (auto &handler : handlers)
        {
            xyce->addOutputInterface(handler);
            std::cout << "handler\n";
        }
        xyce->initializeLate();
        std::cout << "initialised\n";
        thread = kj::heap<kj::Thread>([this]() { this->xyce->runSimulation(); });
    }

    kj::Promise<void> read(ReadContext context)
    {
        OutputHandler* handler = nullptr;
        for (auto &h : handlers)
        {
            if(*h->selected.lockExclusive()) {
                handler = h.get();
                break;
            }
        }
        if(handler == nullptr) {
            context.getResults().setMore(true);
            return kj::READY_NOW;
        }
        auto vecs = handler->vectors.lockExclusive();
        auto res = context.getResults();
        auto dat = res.initData(vecs->size());
        for(size_t h = 0; h< vecs->size(); h++) {

            auto fieldnames = (*vecs)[h].fieldnames;
            auto real_data = (*vecs)[h].real_data;
            auto complex_data = (*vecs)[h].complex_data;

            if(!fieldnames.empty()) {
                dat[h].setScale(fieldnames[0].c_str()); // Xyce prepends TIME / FREQ vector if not given
            }
            res.setMore(*handler->running.lockExclusive());
            auto datlist = dat[h].initData(fieldnames.size());
            for (size_t i = 0; i < fieldnames.size(); i++)
            {
                datlist[i].setName(fieldnames[i]);
                auto dat = datlist[i].getData();
                if (!complex_data[i].empty())
                {
                    auto simdat = complex_data[i];
                    auto list = dat.initComplex(simdat.size());
                    for (size_t j = 0; j < simdat.size(); j++)
                    {
                        list[j].setReal(simdat[j].real());
                        list[j].setImag(simdat[j].imag());
                    }
                }
                else if (!real_data[i].empty())
                {
                    auto simdat = real_data[i];
                    auto list = dat.initReal(simdat.size());
                    for (size_t j = 0; j < simdat.size(); j++)
                    {
                        list.set(j, simdat[j]);
                    }
                } // else no data apparently
                real_data[i].clear();
                complex_data[i].clear();
            }
        }
        return kj::READY_NOW;
    }

    kj::Own<Xyce::Circuit::GenCouplingSimulator> xyce;
    std::vector<kj::Own<OutputHandler>> handlers;
    kj::Own<kj::Thread> thread;
};

class RunImpl final : public Sim::Run::Server
{
public:
    RunImpl(std::string path) : path(path) {}

    kj::Promise<void> run(RunContext context)
    {
        auto cpvecs = context.getParams().getVectors();
        std::vector<std::string> vecs;
        for (auto v : cpvecs)
        {
            vecs.push_back(v);
        }
        Sim::Run::RunResults::Builder res = context.getResults();
        auto reader = kj::heap<ResultImpl>(path, vecs);
        res.setResult(kj::mv(reader));
        return kj::READY_NOW;
    }
    std::string path;
};

typedef Sim::Run SimCommands;
typedef RunImpl SimCommandsImpl;
