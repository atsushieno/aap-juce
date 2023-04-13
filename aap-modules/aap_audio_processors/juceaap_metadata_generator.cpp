
// The code below are used by aap-metadata-generator tool ----------------------------------

#define JUCEAAP_EXPORT_AAP_METADATA_SUCCESS 0
#define JUCEAAP_EXPORT_AAP_METADATA_INVALID_DIRECTORY 1
#define JUCEAAP_EXPORT_AAP_METADATA_INVALID_OUTPUT_FILE 2

void generate_xml_parameter_node(XmlElement *parent, AudioProcessorParameter *para) {
    auto childXml = parent->createNewChildElement("parameter");
    childXml->setAttribute("id", para->getParameterIndex());
    childXml->setAttribute("name", para->getName(1024));
    childXml->setAttribute("direction", "input"); // JUCE does not support output parameter.
    auto ranged = dynamic_cast<RangedAudioParameter *>(para);
    if (ranged) {
        auto range = ranged->getNormalisableRange();
        if (std::isnormal(range.start) || range.start == 0.0)
            childXml->setAttribute("minimum", range.start);
        if (std::isnormal(range.end) || range.end == 0.0)
            childXml->setAttribute("maximum", range.end);
        if (std::isnormal(ranged->getDefaultValue()) || ranged->getDefaultValue() == 0.0)
            childXml->setAttribute("default", range.convertTo0to1(ranged->getDefaultValue()));
    }
    else if (std::isnormal(para->getDefaultValue()))
        childXml->setAttribute("default", para->getDefaultValue());
    childXml->setAttribute("content", "other");
}

void generate_xml_parameter_node(XmlElement *parent,
                                 const AudioProcessorParameterGroup::AudioProcessorParameterNode *node) {
    auto group = node->getGroup();
    if (group != nullptr) {
        auto childXml = parent->createNewChildElement("parameters");
        childXml->setAttribute("xmlns", "urn://androidaudioplugin.org/extensions/parameters");
        childXml->setAttribute("name", group->getName());
        for (auto childPara : *group)
            generate_xml_parameter_node(childXml, childPara);
    } else {
        auto para = node->getParameter();
        generate_xml_parameter_node(parent, para);
    }
}

extern "C" {

__attribute__((used))
__attribute__((visibility("default")))
int generate_aap_metadata(const char *aapMetadataFullPath, const char *library = "libjuce_jni.so",
                          const char *entrypoint = "GetJuceAAPFactory") {

    // FIXME: start application loop

    auto filter = createPluginFilter();
    filter->enableAllBuses();

    String name = "aapports:juce/" + filter->getName();
    auto parameters = filter->getParameters();

    File aapMetadataFile{aapMetadataFullPath};
    if (!aapMetadataFile.getParentDirectory().exists()) {
        std::cerr << "Output directory '" << aapMetadataFile.getParentDirectory().getFullPathName()
                  << "' does not exist." << std::endl;
        return JUCEAAP_EXPORT_AAP_METADATA_INVALID_DIRECTORY;
    }
    std::unique_ptr <juce::XmlElement> pluginsElement{new XmlElement("plugins")};
    pluginsElement->setAttribute("xmlns", "urn:org.androidaudioplugin.core");
    auto pluginElement = pluginsElement->createNewChildElement("plugin");
    pluginElement->setAttribute("name", JucePlugin_Name);
    pluginElement->setAttribute("category", JucePlugin_IsSynth ? "Instrument" : "Effect");
    pluginElement->setAttribute("author", JucePlugin_Manufacturer);
    pluginElement->setAttribute("developer", JucePlugin_Manufacturer);
    pluginElement->setAttribute("unique-id",
                                String::formatted("juceaap:%x", JucePlugin_PluginCode));
    //pluginElement->setAttribute("library", "lib" JucePlugin_Name ".so");
    pluginElement->setAttribute("library", library);
    pluginElement->setAttribute("entrypoint", entrypoint);

    auto topLevelExtensionsElement = pluginElement->createNewChildElement("extensions");
    const char* extensions[] {
        AAP_PLUGIN_INFO_EXTENSION_URI,
        AAP_PRESETS_EXTENSION_URI,
        AAP_STATE_EXTENSION_URI,
        AAP_MIDI_EXTENSION_URI,
        AAP_GUI_EXTENSION_URI
    };
    for (auto ext : extensions) {
        auto extensionElement = topLevelExtensionsElement->createNewChildElement("extension");
        extensionElement->setAttribute("uri", ext);
    }

    auto &tree = filter->getParameterTree();
    auto topLevelParametersElement = pluginElement->createNewChildElement("parameters");
    topLevelParametersElement->setAttribute("xmlns", "urn://androidaudioplugin.org/extensions/parameters");
    bool hadParams = false;
    for (auto node : tree) {
        hadParams = true;
        generate_xml_parameter_node(topLevelParametersElement, node);
    }
    if (!hadParams) {
        for (auto para : filter->getParameters()) {
            hadParams = true;
            generate_xml_parameter_node(topLevelParametersElement, para);
        }
    }
    if (!hadParams) {
        // Some classic plugins (such as OB-Xd) do not return parameter objects.
        // They still return parameter names, so we can still generate AAP parameters.
        for (int i = 0, n = filter->getNumParameters(); i < n; i++) {
            auto childXml = topLevelParametersElement->createNewChildElement("parameter");
            childXml->setAttribute("id", i);
            childXml->setAttribute("name", filter->getParameterName(i));
            childXml->setAttribute("direction", "input"); // JUCE does not support output parameter.
            childXml->setAttribute("minimum", "0.0");
            childXml->setAttribute("maximum", "1.0");
            childXml->setAttribute("default", filter->getParameter(i));
        }
    }

    auto topLevelPortsElement = pluginElement->createNewChildElement("ports");
    auto outChannels = filter->getBusesLayout().getMainOutputChannelSet();
    for (int i = 0; i < outChannels.size(); i++) {
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "output");
        portXml->setAttribute("content", "audio");
        portXml->setAttribute("name",
                              outChannels.getChannelTypeName(outChannels.getTypeOfChannel(i)));
    }
    auto inChannels = filter->getBusesLayout().getMainInputChannelSet();
    for (int i = 0; i < inChannels.size(); i++) {
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "input");
        portXml->setAttribute("content", "audio");
        portXml->setAttribute("name",
                              inChannels.getChannelTypeName(inChannels.getTypeOfChannel(i)));
    }
    if (true) { // Not checking `filter->acceptsMidi()` as parameter changes are sent via MIDI2 in port.
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "input");
        portXml->setAttribute("content", "midi2");
        portXml->setAttribute("name", "MIDI input");
    }
    if (filter->producesMidi()) {
        auto portXml = topLevelPortsElement->createNewChildElement("port");
        portXml->setAttribute("direction", "output");
        portXml->setAttribute("content", "midi2");
        portXml->setAttribute("name", "MIDI output");
    }

    FileOutputStream output{aapMetadataFile};
    if (!output.openedOk()) {
        std::cerr << "Cannot create output file '" << aapMetadataFile.getFullPathName() << "'"
                  << std::endl;
        return JUCEAAP_EXPORT_AAP_METADATA_INVALID_OUTPUT_FILE;
    }
    auto s = pluginsElement->toString();
    output.writeText(s, false, false, "\n");
    output.flush();

    delete filter;

    return JUCEAAP_EXPORT_AAP_METADATA_SUCCESS;
}

}
