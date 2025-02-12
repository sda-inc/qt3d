// Copyright (C) 2017 The Qt Company Ltd and/or its subsidiary(-ies).
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only

#include "plygeometryloader.h"

#include <QtCore/QDataStream>
#include <QtCore/QLoggingCategory>
#include <QtCore/QIODevice>

QT_BEGIN_NAMESPACE

namespace Qt3DRender {

Q_LOGGING_CATEGORY(PlyGeometryLoaderLog, "Qt3D.PlyGeometryLoader", QtWarningMsg)

namespace {

class PlyDataReader
{
public:
    virtual ~PlyDataReader() {}

    virtual int readIntValue(PlyGeometryLoader::DataType type) = 0;
    virtual float readFloatValue(PlyGeometryLoader::DataType type) = 0;
};

class AsciiPlyDataReader : public PlyDataReader
{
public:
    AsciiPlyDataReader(QIODevice *ioDev)
        : m_stream(ioDev)
    { }

    int readIntValue(PlyGeometryLoader::DataType) override
    {
        int value;
        m_stream >> value;
        return value;
    }

    float readFloatValue(PlyGeometryLoader::DataType) override
    {
        float value;
        m_stream >> value;
        return value;
    }

private:
    QTextStream m_stream;
};

class BinaryPlyDataReader : public PlyDataReader
{
public:
    BinaryPlyDataReader(QIODevice *ioDev, QDataStream::ByteOrder byteOrder)
        : m_stream(ioDev)
    {
        ioDev->setTextModeEnabled(false);
        m_stream.setByteOrder(byteOrder);
    }

    int readIntValue(PlyGeometryLoader::DataType type) override
    {
        return readValue<int>(type);
    }

    float readFloatValue(PlyGeometryLoader::DataType type) override
    {
        return readValue<float>(type);
    }

private:
    template <typename T>
    T readValue(PlyGeometryLoader::DataType type)
    {
        switch (type) {
        case PlyGeometryLoader::Int8:
        {
            qint8 value;
            m_stream >> value;
            return value;
        }

        case PlyGeometryLoader::Uint8:
        {
            quint8 value;
            m_stream >> value;
            return value;
        }

        case PlyGeometryLoader::Int16:
        {
            qint16 value;
            m_stream >> value;
            return value;
        }

        case PlyGeometryLoader::Uint16:
        {
            quint16 value;
            m_stream >> value;
            return value;
        }

        case PlyGeometryLoader::Int32:
        {
            qint32 value;
            m_stream >> value;
            return value;
        }

        case PlyGeometryLoader::Uint32:
        {
            quint32 value;
            m_stream >> value;
            return value;
        }

        case PlyGeometryLoader::Float32:
        {
            m_stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
            float value;
            m_stream >> value;
            return value;
        }

        case PlyGeometryLoader::Float64:
        {
            m_stream.setFloatingPointPrecision(QDataStream::DoublePrecision);
            double value;
            m_stream >> value;
            return value;
        }

        default:
            break;
        }

        return 0;
    }

    QDataStream m_stream;
};

}

static PlyGeometryLoader::DataType toPlyDataType(const QString &typeName)
{
    if (typeName == QStringLiteral("int8") || typeName == QStringLiteral("char")) {
        return PlyGeometryLoader::Int8;
    } else if (typeName == QStringLiteral("uint8") || typeName == QStringLiteral("uchar")) {
        return PlyGeometryLoader::Uint8;
    } else if (typeName == QStringLiteral("int16") || typeName == QStringLiteral("short")) {
        return PlyGeometryLoader::Int16;
    } else if (typeName == QStringLiteral("uint16") || typeName == QStringLiteral("ushort")) {
        return PlyGeometryLoader::Uint16;
    } else if (typeName == QStringLiteral("int32") || typeName == QStringLiteral("int")) {
        return PlyGeometryLoader::Int32;
    } else if (typeName == QStringLiteral("uint32") || typeName == QStringLiteral("uint")) {
        return PlyGeometryLoader::Uint32;
    } else if (typeName == QStringLiteral("float32") || typeName == QStringLiteral("float")) {
        return PlyGeometryLoader::Float32;
    } else if (typeName == QStringLiteral("float64") || typeName == QStringLiteral("double")) {
        return PlyGeometryLoader::Float64;
    } else if (typeName == QStringLiteral("list")) {
        return PlyGeometryLoader::TypeList;
    } else {
        return PlyGeometryLoader::TypeUnknown;
    }
}

bool PlyGeometryLoader::doLoad(QIODevice *ioDev, const QString &subMesh)
{
    Q_UNUSED(subMesh);

    if (!parseHeader(ioDev))
        return false;

    if (!parseMesh(ioDev))
        return false;

    return true;
}

/*!
    Read and parse the header of the PLY format file.
    Returns \c false if one of the lines is wrongly
    formatted.
*/
bool PlyGeometryLoader::parseHeader(QIODevice *ioDev)
{
    Format format = FormatUnknown;

    m_hasNormals = m_hasTexCoords = false;

    while (!ioDev->atEnd()) {
        QByteArray lineBuffer = ioDev->readLine();
        QTextStream textStream(lineBuffer, QIODevice::ReadOnly);

        QString token;
        textStream >> token;

        if (token == QStringLiteral("end_header")) {
            break;
        } else if (token == QStringLiteral("format")) {
            QString formatName;
            textStream >> formatName;

            if (formatName == QStringLiteral("ascii")) {
                m_format = FormatAscii;
            } else if (formatName == QStringLiteral("binary_little_endian")) {
                m_format = FormatBinaryLittleEndian;
            } else if (formatName == QStringLiteral("binary_big_endian")) {
                m_format = FormatBinaryBigEndian;
            } else {
                qCDebug(PlyGeometryLoaderLog) << "Unrecognized PLY file format" << format;
                return false;
            }
        } else if (token == QStringLiteral("element")) {
            Element element;

            QString elementName;
            textStream >> elementName;

            if (elementName == QStringLiteral("vertex")) {
                element.type = ElementVertex;
            } else if (elementName == QStringLiteral("face")) {
                element.type = ElementFace;
            } else {
                element.type = ElementUnknown;
            }

            textStream >> element.count;

            m_elements.append(element);
        } else if (token == QStringLiteral("property")) {
            if (m_elements.isEmpty()) {
                qCDebug(PlyGeometryLoaderLog) << "Misplaced property in header";
                return false;
            }

            Property property;

            QString dataTypeName;
            textStream >> dataTypeName;

            property.dataType = toPlyDataType(dataTypeName);

            if (property.dataType == TypeList) {
                QString listSizeTypeName;
                textStream >> listSizeTypeName;

                property.listSizeType = toPlyDataType(listSizeTypeName);

                QString listElementTypeName;
                textStream >> listElementTypeName;

                property.listElementType = toPlyDataType(listElementTypeName);
            }

            QString propertyName;
            textStream >> propertyName;

            if (propertyName == QStringLiteral("vertex_index")) {
                property.type = PropertyVertexIndex;
            } else if (propertyName == QStringLiteral("x")) {
                property.type = PropertyX;
            } else if (propertyName == QStringLiteral("y")) {
                property.type = PropertyY;
            } else if (propertyName == QStringLiteral("z")) {
                property.type = PropertyZ;
            } else if (propertyName == QStringLiteral("nx")) {
                property.type = PropertyNormalX;
                m_hasNormals = true;
            } else if (propertyName == QStringLiteral("ny")) {
                property.type = PropertyNormalY;
                m_hasNormals = true;
            } else if (propertyName == QStringLiteral("nz")) {
                property.type = PropertyNormalZ;
                m_hasNormals = true;
            } else if (propertyName == QStringLiteral("s")) {
                property.type = PropertyTextureU;
                m_hasTexCoords = true;
            } else if (propertyName == QStringLiteral("t")) {
                property.type = PropertyTextureV;
                m_hasTexCoords = true;
            } else {
                property.type = PropertyUnknown;
            }

            Element &element = m_elements.last();
            element.properties.append(property);
        }
    }

    if (m_format == FormatUnknown) {
        qCDebug(PlyGeometryLoaderLog) << "Missing PLY file format";
        return false;
    }

    return true;
}

bool PlyGeometryLoader::parseMesh(QIODevice *ioDev)
{
    QScopedPointer<PlyDataReader> dataReader;

    switch (m_format) {
    case FormatAscii:
        dataReader.reset(new AsciiPlyDataReader(ioDev));
        break;

    case FormatBinaryLittleEndian:
        dataReader.reset(new BinaryPlyDataReader(ioDev, QDataStream::LittleEndian));
        break;

    default:
        dataReader.reset(new BinaryPlyDataReader(ioDev, QDataStream::BigEndian));
        break;
    }

    for (auto &element : qAsConst(m_elements)) {
        if (element.type == ElementVertex) {
            m_points.reserve(element.count);

            if (m_hasNormals)
                m_normals.reserve(element.count);

            if (m_hasTexCoords)
                m_texCoords.reserve(element.count);
        }

        for (int i = 0; i < element.count; ++i) {
            QVector3D point;
            QVector3D normal;
            QVector2D texCoord;

            QList<unsigned int> faceIndices;

            for (auto &property : element.properties) {
                if (property.dataType == TypeList) {
                    const int listSize = dataReader->readIntValue(property.listSizeType);

                    if (element.type == ElementFace)
                        faceIndices.reserve(listSize);

                    for (int j = 0; j < listSize; ++j) {
                        const unsigned int value = dataReader->readIntValue(property.listElementType);

                        if (element.type == ElementFace)
                            faceIndices.append(value);
                    }
                } else {
                    float value = dataReader->readFloatValue(property.dataType);

                    if (element.type == ElementVertex) {
                        switch (property.type) {
                        case PropertyX: point.setX(value); break;
                        case PropertyY: point.setY(value); break;
                        case PropertyZ: point.setZ(value); break;
                        case PropertyNormalX: normal.setX(value); break;
                        case PropertyNormalY: normal.setY(value); break;
                        case PropertyNormalZ: normal.setZ(value); break;
                        case PropertyTextureU: texCoord.setX(value); break;
                        case PropertyTextureV: texCoord.setY(value); break;
                        default: break;
                        }
                    }
                }
            }

            if (element.type == ElementVertex) {
                m_points.push_back(point);

                if (m_hasNormals)
                    m_normals.push_back(normal);

                if (m_hasTexCoords)
                    m_texCoords.push_back(texCoord);
            } else if (element.type == ElementFace) {
                if (faceIndices.size() >= 3) {
                    // decompose face into triangle fan

                    for (int j = 1; j < faceIndices.size() - 1; ++j) {
                        m_indices.push_back(faceIndices[0]);
                        m_indices.push_back(faceIndices[j]);
                        m_indices.push_back(faceIndices[j + 1]);
                    }
                }
            }
        }
    }

    return true;
}

/*!
   \enum Qt3DRender::PlyGeometryLoader::DataType

   Specifies the data type specified in the parsed file.

   \value Int8
   \value Uint8
   \value Int16
   \value Uint16
   \value Int32
   \value Uint32
   \value Float32
   \value Float64
   \value TypeList
   \value TypeUnknown
*/
/*!
    \enum Qt3DRender::PlyGeometryLoader::Format

    Specifies the format mentioned in the header of the parsed file.

    \value FormatAscii
    \value FormatBinaryLittleEndian
    \value FormatBinaryBigEndian
    \value FormatUnknown
*/
/*!
    \enum Qt3DRender::PlyGeometryLoader::ElementType

    Specifies the element type mentioned in the header of the file.

    \value ElementVertex
    \value ElementFace
    \value ElementUnknown

*/
/*!
    \enum Qt3DRender::PlyGeometryLoader::PropertyType

    Specifies the property type from the PLY format file that has been loaded.

    \value PropertyVertexIndex Property name in header is \c vertex_index
    \value PropertyX Property name in header is \c X
    \value PropertyY Property name in header is \c Y
    \value PropertyZ Property name in header is \c Z
    \value PropertyNormalX Property name in header is \c NormalX
    \value PropertyNormalY Property name in header is \c NormalY
    \value PropertyNormalZ Property name in header is \c NormalZ
    \value PropertyTextureU Property name in header is \c TextureU
    \value PropertyTextureV Property name in header is \c TextureV
    \value PropertyUnknown Property name in header is unknown

*/
} // namespace Qt3DRender

QT_END_NAMESPACE
