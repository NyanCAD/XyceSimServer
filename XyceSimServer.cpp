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

void report_handler(const char *message, unsigned type) {
    std::cout << message;
}

class OutputHandler final : public Xyce::IO::ExternalOutputInterface
{
    public:
    void requestedOutputs(std::vector<std::string> & outputVars) {
        outputVars.push_back("V(*)");
        outputVars.push_back("I(*)");
        outputVars.push_back("TIME");
    };

    void outputFieldNames(std::vector<std::string> & outputNames) {
        fieldnames = outputNames;
        real_data.resize(outputNames.size());
    }

    void outputReal(std::vector<double> & outputData) {
        for (int i=0; i < outputData.size(); i++) {
            real_data[i].push_back(outputData[i]);
        }
    }

    void outputComplex(std::vector<std::complex<double>> & outputData) {
        for (int i=0; i < outputData.size(); i++) {
            complex_data[i].push_back(outputData[i]);
        }
    }

    void clear() {
        for (auto data : real_data) {
            data.clear();
        }
    }

    std::vector<std::string> fieldnames;
    std::vector<std::vector<double>> real_data;
    std::vector<std::vector<std::complex<double>>> complex_data;
};

class ResultImpl final : public Simulator::Result::Server
{
public:
    ResultImpl(kj::Own<Xyce::Circuit::GenCouplingSimulator>&& xyceref) : xyce(kj::mv(xyceref)) {
        handler = kj::heap<OutputHandler>();
        xyce->addOutputInterface(handler); // handles only one type, currently TRAN
    }

    kj::Promise<void> read(ReadContext context) {

        return kj::READY_NOW;
    }
    kj::Promise<void> readTime(ReadTimeContext context) {

        return kj::READY_NOW;
    }
    kj::Promise<void> readAll(ReadAllContext context) {
        xyce->runSimulation();
        size_t size = handler->fieldnames.size();
        auto res = context.getResults().initData(size);
        for (size_t i=0; i<size; i++) {
            res[i].setName(handler->fieldnames[i]);
            auto dat = res[i].getData();
            auto simdat = handler->real_data[i];
            auto list = dat.initReal(simdat.size());
            for (size_t j=0; j<simdat.size(); j++) {
                list.set(j, simdat[j]);
            }
        }
        return kj::READY_NOW;
    }
    kj::Promise<void> seek(SeekContext context) {

        return kj::READY_NOW;
    }

    kj::Own<Xyce::Circuit::GenCouplingSimulator> xyce;
    kj::Own<OutputHandler> handler;
    size_t index = 0;
};

class RunImpl final : public Simulator::Run::Server
{
public:
    RunImpl(std::vector<std::string> args) : args(args) {}

    kj::Promise<void> run(RunContext context) {
        auto xyce = kj::heap<Xyce::Circuit::GenCouplingSimulator>();
        Xyce::set_report_handler(report_handler);
        std::vector<const char*> tmpargs;
        for (auto a : args) { tmpargs.push_back(a.c_str()); }
        try {
            xyce->initialize(tmpargs.size(), const_cast<char**>(tmpargs.data()));
        } catch (std::runtime_error &e) {
            xyce = nullptr;
            e = std::runtime_error("Fatal error in parsing netlist, check logs");
            throw;
        }
        Simulator::Run::RunResults::Builder res = context.getResults();
        auto reader = kj::heap<ResultImpl>(kj::mv(xyce));
        res.setResult(kj::mv(reader));
        return kj::READY_NOW;
    }
    std::vector<std::string> args;
};

class SimulatorImpl final : public Simulator::Server
{
public:
    SimulatorImpl(const kj::Directory& dir, std::vector<std::string> args) : dir(dir), args(args) {}

    kj::Promise<void> loadFiles(LoadFilesContext context) override
    {
        auto files = context.getParams().getFiles();
        for (Simulator::File::Reader f : files) {
            kj::Path path = kj::Path::parse(f.getName());
            kj::Own<const kj::File> file = dir.openFile(path, kj::WriteMode::CREATE|kj::WriteMode::MODIFY);
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
    const kj::Directory& dir;

};

int main(int argc, const char *argv[])
{
    std::vector<std::string> arguments(argv, argv + argc);

    kj::Own<kj::Filesystem> fs = kj::newDiskFilesystem();
    const kj::Directory& dir = fs->getCurrent();

    // Set up a server.
    capnp::EzRpcServer server(kj::heap<SimulatorImpl>(dir, arguments), "*:5923");

    auto &waitScope = server.getWaitScope();
    uint port = server.getPort().wait(waitScope);
    std::cout << "Listening on port " << port << "..." << std::endl;

    // Run forever, accepting connections and handling requests.
    kj::NEVER_DONE.wait(waitScope);
}
