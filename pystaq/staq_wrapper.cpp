/*
 * This file is part of pystaq.
 *
 * Copyright (c) 2019 - 2022 softwareQ Inc. All rights reserved.
 *
 * MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <pybind11/pybind11.h>
#include <sstream>

#include "qasmtools/parser/parser.hpp"

#include "transformations/desugar.hpp"
#include "transformations/inline.hpp"
#include "transformations/oracle_synthesizer.hpp"
#include "transformations/barrier_merge.hpp"
#include "transformations/expression_simplifier.hpp"

#include "optimization/simplify.hpp"
#include "optimization/rotation_folding.hpp"
#include "optimization/cnot_resynthesis.hpp"

#include "mapping/device.hpp"
#include "mapping/layout/basic.hpp"
#include "mapping/layout/eager.hpp"
#include "mapping/layout/bestfit.hpp"
#include "mapping/mapping/swap.hpp"
#include "mapping/mapping/steiner.hpp"

#include "tools/resource_estimator.hpp"
#include "tools/qubit_estimator.hpp"

#include "output/projectq.hpp"
#include "output/qsharp.hpp"
#include "output/quil.hpp"
#include "output/cirq.hpp"

namespace py = pybind11;

class Program {
    qasmtools::ast::ptr<qasmtools::ast::Program> prog_;
  public:
    Program(qasmtools::ast::ptr<qasmtools::ast::Program> prog)
        : prog_(std::move(prog)) {}
    /**
     * \brief Print the formatted QASM source code
     */
    friend std::ostream& operator<<(std::ostream& os, const Program& p) {
        return os << *(p.prog_);
    }
    // transformations/optimizations/etc.
    void desugar() {
        staq::transformations::desugar(*prog_);
    }
    void inline_prog(bool clear_decls = false, bool inline_stdlib = false,
                     const std::string& ancilla_name = "anc") {
        using namespace staq;
        std::set<std::string_view> overrides =
                inline_stdlib ? std::set<std::string_view>()
                              : transformations::default_overrides;
        transformations::inline_ast(*prog_,
                                    {!clear_decls, overrides, ancilla_name});
    }
    void map(const std::string& layout = "linear",
             const std::string& mapper = "swap", bool evaluate_all = false,
             const std::string& device_json = "") {
        using namespace staq;
        // Inline fully first
        transformations::inline_ast(*prog_, {false, {}, "anc"});

        // Physical device
        mapping::Device dev;
        if (!device_json.empty()) {
            dev = mapping::parse_json(device_json);
        } else {
            dev = mapping::fully_connected(tools::estimate_qubits(*prog_));
        }

        // Initial layout
        mapping::layout physical_layout;
        if (layout == "linear") {
            physical_layout = mapping::compute_basic_layout(dev, *prog_);
        } else if (layout == "eager") {
            physical_layout = mapping::compute_eager_layout(dev, *prog_);
        } else if (layout == "bestfit") {
            physical_layout = mapping::compute_bestfit_layout(dev, *prog_);
        } else {
            std::cerr << "Error: invalid layout algorithm\n";
            return;
        }
        mapping::apply_layout(physical_layout, dev, *prog_);

        // Mapping
        if (mapper == "swap") {
            mapping::map_onto_device(dev, *prog_);
        } else if (mapper == "steiner") {
            mapping::steiner_mapping(dev, *prog_);
        } else {
            std::cerr << "Error: invalid mapping algorithm\n";
            return;
        }

        /* Evaluating symbolic expressions */
        if (evaluate_all) {
            transformations::expr_simplify(*prog_, true);
        }
    }
    void rotation_fold(bool no_correction = false) {
        staq::optimization::fold_rotations(*prog_, {!no_correction});
    }
    void simplify(bool no_fixpoint = false) {
        staq::transformations::expr_simplify(*prog_);
        staq::optimization::simplify(*prog_, {!no_fixpoint});
    }
    void synthesize_oracles() {
        staq::transformations::synthesize_oracles(*prog_);
    }
    // output (these methods return a string)
    std::string get_resources(bool box_gates = false,
                              bool unbox_qelib = false,
                              bool no_merge_dagger = false) {
        std::set<std::string_view> overrides =
                unbox_qelib ? std::set<std::string_view>()
                            : qasmtools::ast::qelib_defs;
        auto count = staq::tools::estimate_resources(
                *prog_, {!box_gates, !no_merge_dagger, overrides});

        std::ostringstream oss;
        oss << "Resources used:\n";
        for (auto& [name, num] : count) {
            oss << "  " << name << ": " << num << "\n";
        }
        return oss.str();
    }
    std::string to_cirq() {
        std::ostringstream oss;
        staq::output::CirqOutputter outputter(oss);
        outputter.run(*prog_);
        return oss.str();
    }
    std::string to_projectq() {
        std::ostringstream oss;
        staq::output::ProjectQOutputter outputter(oss);
        outputter.run(*prog_);
        return oss.str();
    }
    std::string to_qsharp() {
        std::ostringstream oss;
        staq::output::QSharpOutputter outputter(oss);
        outputter.run(*prog_);
        return oss.str();
    }
    std::string to_quil() {
        std::ostringstream oss;
        staq::output::QuilOutputter outputter(oss);
        outputter.run(*prog_);
        return oss.str();
    }
};

Program parse_str(const std::string& s) {
    return qasmtools::parser::parse_string(s);
}
Program parse_file(const std::string& fname) {
    return qasmtools::parser::parse_file(fname);
}

void desugar(Program& prog) {
    prog.desugar();
}
void inline_prog(Program& prog, bool clear_decls, bool inline_stdlib,
                 const std::string& ancilla_name) {
    prog.inline_prog(clear_decls, inline_stdlib, ancilla_name);
}
void map(Program& prog, const std::string& layout, const std::string& mapper,
         bool evaluate_all, const std::string& device_json) {
    prog.map(layout, mapper, evaluate_all, device_json);
}
void rotation_fold(Program& prog, bool no_correction) {
    prog.rotation_fold(no_correction);
}
void simplify(Program& prog, bool no_fixpoint) {
    prog.simplify(no_fixpoint);
}
void synthesize_oracles(Program& prog) {
    prog.synthesize_oracles();
}



static double FIDELITY_1 = staq::mapping::FIDELITY_1;

class Device {
    int n_;
    std::vector<double> sq_fi_;
    std::vector<std::vector<bool>> adj_;
    std::vector<std::vector<double>> tq_fi_;
  public:
    Device(int n)
        : n_(n), sq_fi_(n_, FIDELITY_1), adj_(n_, std::vector<bool>(n_)),
          tq_fi_(n_, std::vector<double>(n_, FIDELITY_1)) {
        if (n_ <= 0)
            throw std::logic_error("Invalid device qubit count");
    }
    void add_edge(int control, int target, bool directed = false,
                  double fidelity = FIDELITY_1) {
        if (control < 0 || control >= n_ || target < 0 || target >= n_)
            std::cerr << "Qubit(s) out of range: " << control << "," << target
                      << "\n";
        else {
            adj_[control][target] = true;
            if (!directed)
                adj_[target][control] = true;
            if (fidelity != FIDELITY_1) {
                if (fidelity < 0 || fidelity > 1)
                    std::cerr << "Fidelity out of range: " << fidelity << "\n";
                else {
                    tq_fi_[control][target] = fidelity;
                    if (!directed)
                        tq_fi_[target][control] = fidelity;
                }
            }
        }
    }
    void set_fidelity(int qubit, double fidelity) {
        if (qubit < 0 || qubit >= n_)
            std::cerr << "Qubit out of range: " << qubit;
        else if (fidelity < 0 || fidelity > 1)
            std::cerr << "Fidelity out of range: " << fidelity;
        else
            sq_fi_[qubit] = fidelity;
    }
    std::string to_string() const {
        staq::mapping::Device dev("Custom Device", n_, adj_, sq_fi_, tq_fi_);
        return dev.to_json();
    }
};



PYBIND11_MODULE(pystaq, m) {
    m.doc() = "Python wrapper for staq (https://github.com/softwareQinc/staq)";

    py::class_<Program>(m, "Program")
        .def("get_resources", &Program::get_resources, "Get circuit statistics",
             py::arg("box_gates") = false, py::arg("unbox_qelib") = false,
             py::arg("no_merge_dagger") = false)
        .def("to_cirq", &Program::to_cirq, "Get the Cirq representation")
        .def("to_projectq", &Program::to_projectq,
             "Get the ProjectQ representation")
        .def("to_qsharp", &Program::to_qsharp, "Get the Q# representation")
        .def("to_quil", &Program::to_quil, "Get the Quil representation")
        .def("__repr__", [](const Program& p){
            std::ostringstream oss;
            oss << p;
            return oss.str();
        });

    m.def("parse_str", &parse_str, "Parse OpenQASM program string");
    m.def("parse_file", &parse_file, "Parse OpenQASM program file");
    m.def("desugar", &desugar, "Expand out gates applied to registers");
    m.def("inline", &inline_prog, "Inline the OpenQASM source code",
          py::arg("prog"), py::arg("clear_decls") = false,
          py::arg("inline_stdlib") = false, py::arg("ancilla_name") = "anc");
    m.def("map", &map, "Map circuit to a physical device",
          py::arg("prog"), py::arg("layout") = "linear",
          py::arg("mapper") = "swap", py::arg("evaluate_all") = false,
          py::arg("device_json") = "");
    m.def("rotation_fold", &rotation_fold,
          "Reduce the number of small-angle rotation gates in all Pauli bases",
          py::arg("prog"), py::arg("no_correction") = false);
    m.def("simplify", &simplify, "Apply basic circuit simplifications",
          py::arg("prog"), py::arg("no_fixpoint") = false);
    m.def("synthesize_oracles", &synthesize_oracles,
          "Synthesizes oracles declared by verilog files");

    py::class_<Device>(m, "Device")
        .def(py::init<int>())
        .def("add_edge", &Device::add_edge, "Add edge with optional fidelity",
             py::arg("control"), py::arg("target"), py::arg("directed") = false,
             py::arg("fidelity") = FIDELITY_1)
        .def("set_fidelity", &Device::set_fidelity, "Set single qubit fidelity")
        .def("__repr__", [](const Device& d){
            return d.to_string();
        });
}
