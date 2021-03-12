#include "Simulator.capnp.h"
#include <kj/debug.h>
#include <kj/filesystem.h>
#include <kj/exception.h>
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

class OutputHandler final : public Xyce::IO::ExternalOutputInterface
{
public:
    OutputHandler(std::string name, Xyce::IO::OutputType::OutputType type, std::vector<std::string> outputs)
        : requested_fieldnames(outputs), type(type), name(name) {}

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
        for (int i = 0; i < numsteps; i++)
        {
            for (auto field : outputNames)
            {
                auto num = std::to_string(i);
                fieldnames.push_back(name + num + "_" + field);
            }
        }
        real_data.resize(outputNames.size() * numsteps);
        complex_data.resize(outputNames.size() * numsteps);
    }

    void newStepOutput(int stepNumber, int maxStep)
    {
        step = stepNumber;
        numsteps = maxStep;
    }

    void outputReal(std::vector<double> &outputData)
    {
        for (int i = 0; i < outputData.size(); i++)
        {
            int idx = outputData.size() * step + i;
            real_data[idx].push_back(outputData[i]);
        }
    }

    void outputComplex(std::vector<std::complex<double>> &outputData)
    {
        for (int i = 0; i < outputData.size(); i++)
        {
            int idx = outputData.size() * step + i;
            complex_data[idx].push_back(outputData[i]);
        }
    }

    void clear()
    {
        for (auto data : real_data)
        {
            data.clear();
        }
    }

    std::string name;
    int step = 0;
    int numsteps = 1;
    Xyce::IO::OutputType::OutputType type;
    std::vector<std::string> requested_fieldnames;
    std::vector<std::string> fieldnames;
    std::vector<std::vector<double>> real_data;
    std::vector<std::vector<std::complex<double>>> complex_data;
};

class ResultImpl final : public Simulator::Result::Server
{
public:
    ResultImpl(kj::Own<Xyce::Circuit::GenCouplingSimulator> &&xyceref, std::vector<std::string> vecs) : xyce(kj::mv(xyceref))
    {
        handlers.push_back(kj::heap<OutputHandler>("tran", Xyce::IO::OutputType::TRAN, vecs));
        handlers.push_back(kj::heap<OutputHandler>("ac", Xyce::IO::OutputType::AC, vecs));
        handlers.push_back(kj::heap<OutputHandler>("op", Xyce::IO::OutputType::DCOP, vecs));
        handlers.push_back(kj::heap<OutputHandler>("dc", Xyce::IO::OutputType::DC, vecs));
        handlers.push_back(kj::heap<OutputHandler>("noise", Xyce::IO::OutputType::NOISE, vecs));
        // ...
        for (auto &handler : handlers)
        {
            xyce->addOutputInterface(handler);
        }
    }

    kj::Promise<void> read(ReadContext context)
    {

        return kj::READY_NOW;
    }
    kj::Promise<void> readTime(ReadTimeContext context)
    {

        return kj::READY_NOW;
    }
    kj::Promise<void> readAll(ReadAllContext context)
    {
        xyce->runSimulation();

        int totalfields = 0;
        for (auto &handler : handlers)
        {
            totalfields += handler->fieldnames.size();
        }
        auto res = context.getResults().initData(totalfields);
        for (auto &handler : handlers)
        {
            size_t size = handler->fieldnames.size();
            for (size_t i = 0; i < size; i++)
            {
                res[i].setName(handler->fieldnames[i]);
                auto dat = res[i].getData();
                if (!handler->complex_data[i].empty())
                {
                    auto simdat = handler->complex_data[i];
                    auto list = dat.initComplex(simdat.size());
                    for (size_t j = 0; j < simdat.size(); j++)
                    {
                        list[j].setReal(simdat[j].real());
                        list[j].setImag(simdat[j].imag());
                    }
                }
                else if (!handler->real_data[i].empty())
                {
                    auto simdat = handler->real_data[i];
                    auto list = dat.initReal(simdat.size());
                    for (size_t j = 0; j < simdat.size(); j++)
                    {
                        list.set(j, simdat[j]);
                    }
                } // else no data apparently
            }
        }
        return kj::READY_NOW;
    }
    kj::Promise<void> seek(SeekContext context)
    {

        return kj::READY_NOW;
    }

    kj::Own<Xyce::Circuit::GenCouplingSimulator> xyce;
    std::vector<kj::Own<OutputHandler>> handlers;
};

class RunImpl final : public Simulator::Run::Server
{
public:
    RunImpl(std::vector<std::string> args) : args(args) {}

    kj::Promise<void> run(RunContext context)
    {
        auto xyce = kj::heap<Xyce::Circuit::GenCouplingSimulator>();
        Xyce::set_report_handler(report_handler);
        std::vector<const char *> tmpargs;
        for (auto a : args)
        {
            tmpargs.push_back(a.c_str());
        }
        try
        {
            xyce->initialize(tmpargs.size(), const_cast<char **>(tmpargs.data()));
        }
        catch (std::runtime_error &e)
        {
            xyce = nullptr;
            e = std::runtime_error("Fatal error in parsing netlist, check logs");
            throw;
        }
        auto cpvecs = context.getParams().getVectors();
        std::vector<std::string> vecs;
        for (auto v : cpvecs)
        {
            vecs.push_back(v);
        }
        Simulator::Run::RunResults::Builder res = context.getResults();
        auto reader = kj::heap<ResultImpl>(kj::mv(xyce), vecs);
        res.setResult(kj::mv(reader));
        return kj::READY_NOW;
    }
    std::vector<std::string> args;
};

class SimulatorImpl final : public Simulator::Server
{
public:
    SimulatorImpl(const kj::Directory &dir, std::vector<std::string> args) : dir(dir), args(args) {}

    kj::Promise<void> loadFiles(LoadFilesContext context) override
    {
        auto files = context.getParams().getFiles();
        for (Simulator::File::Reader f : files)
        {
            kj::Path path = kj::Path::parse(f.getName());
            kj::Own<const kj::File> file = dir.openFile(path, kj::WriteMode::CREATE | kj::WriteMode::MODIFY);
            file->truncate(0);
            file->write(0, f.getContents());
        }

        std::vector<std::string> tmpargs = args;
        tmpargs.push_back(files[0].getName());

        Simulator::LoadFilesResults::Builder res = context.getResults();
        auto cmd = res.initCommands(1);
        auto runner = kj::heap<RunImpl>(tmpargs);
        cmd[0].setRun(kj::mv(runner));
        return kj::READY_NOW;
    }

    std::vector<std::string> args;
    const kj::Directory &dir;
};

int main(int argc, const char *argv[])
{
    std::vector<std::string> arguments(argv, argv + argc);

    kj::Own<kj::Filesystem> fs = kj::newDiskFilesystem();
    const kj::Directory &dir = fs->getCurrent();

    // Set up a server.
    capnp::EzRpcServer server(kj::heap<SimulatorImpl>(dir, arguments), "*:5923");

    auto &waitScope = server.getWaitScope();
    uint port = server.getPort().wait(waitScope);
    std::cout << "Listening on port " << port << "..." << std::endl;

    // Run forever, accepting connections and handling requests.
    kj::NEVER_DONE.wait(waitScope);
}
